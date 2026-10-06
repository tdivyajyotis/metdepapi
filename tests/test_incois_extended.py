import gzip
import io
import json
import math
import tempfile
import unittest
from datetime import datetime, timezone
from pathlib import Path
from unittest.mock import patch

from imd_local.core import CollectionError, Result, Store, Transport
from imd_local.cli import handler
from imd_local.incois import normalize, subset_rows, source_unit, forecast_quality
from imd_local.incois.native import locations, chart_rows, collect_native
from imd_local.incois.view import marine_view


NOW = datetime(2026, 10, 5, tzinfo=timezone.utc)


class ExtendedIncoisTests(unittest.TestCase):
    def test_subset_coordinates_distance_and_completeness(self):
        text = 'time,latitude[unit="degrees_north"],longitude[unit="degrees_east"],HS[unit=""]\n2026-10-05T00:00:00Z,19.2,84.9,1.5\n'
        rows = subset_rows(text, 'HS', [NOW], 19.24, 84.94, [], 20)
        self.assertAlmostEqual(rows[0]['grid_cell_distance_km'], 6.117, places=2)
        self.assertIsNone(subset_rows(text, 'HS', [NOW], 19.24, 84.94, [], 1)[0]['value'])
        with self.assertRaises(CollectionError):
            subset_rows(text.replace('2026-10-05', '2026-10-06'), 'HS', [NOW], 19.24, 84.94, [], 20)
        with self.assertRaises(CollectionError):
            subset_rows(text+'2026-10-05T00:00:00Z,19.2,84.9,2\n', 'HS', [NOW], 19.24, 84.94, [], 20)

    def test_normalization_requires_units_and_matched_vector_cells(self):
        rows = [{'layer': 'DIR', 'units': 'rad', 'value': math.pi, 'valid_time': NOW.isoformat()},
                {'layer': 'PWP', 'units': None, 'value': 12, 'valid_time': NOW.isoformat()}]
        normalize(rows)
        self.assertEqual(rows[0]['normalized']['wave_dir_deg'], 180)
        self.assertEqual(rows[1]['normalized'], {})
        vector = [{'layer': n, 'units': 'm/s', 'value': v, 'valid_time': NOW.isoformat(),
                   'grid_cell_coordinates': {'lat': 19.2, 'lon': 84.9}} for n, v in [('UVEL', 3), ('VVEL', 4)]]
        self.assertEqual(normalize(vector)[0]['value'], 5)
        vector[1]['grid_cell_coordinates']['lat'] = 19.3
        self.assertEqual(normalize(vector), [])
        self.assertEqual(source_unit({'long_name': 'Sea Surface Temperature (Deg. C.)'})[0], 'Deg. C.')

    def test_registry_without_semicolon_and_following_script(self):
        text = 'const flcLocations = [{id: 1024, name: "Gopalpur", lat: 19.255028, lon: 84.90646}]\nlet other = true;'
        self.assertEqual(locations(text)[0]['id'], 1024)
        with self.assertRaises(CollectionError):
            locations(text.replace('84.90646', '999'))

    def test_buoy_chart_units_window_and_missing_qc(self):
        text = '<input name="buoy" value="Gopalpur"> useUTC: true; yAxis: {text: ["Cm"]}; series: [{name: "WRB Data", data: [[1000,120],[2000,130]]}]'
        rows, count = chart_rows(text, 'Gopalpur', 'hm0', 1)
        self.assertEqual(count, 2)
        self.assertEqual(rows[0]['normalized']['hs_m'], 1.3)
        self.assertIsNone(rows[0]['qc'])
        with self.assertRaises(CollectionError):
            chart_rows(text.replace('useUTC: true', 'useUTC: false'), 'Gopalpur', 'hm0', 1)

    def test_duplicate_source_registry_ids_are_preserved(self):
        rows = locations('const flcLocations = [{id: 1, name: "A", lat: 1, lon: 2}, {id: 1, name: "B", lat: 2, lon: 3}]')
        self.assertEqual(len(rows), 2)

    def test_tide_registry_identity_empty_series_and_native_time(self):
        class Source:
            def text(self, url):
                return '<stations><station status="Not Reporting"><statrealName>Gopalpur</statrealName><latitude>19.2889</latitude><longitude>84.9483</longitude></station></stations>'
            def json(self, url):
                return []
        result, _ = collect_native('tide-gauge', {}, Source(), 'unused')
        self.assertEqual(result.metadata['availability'], 'no_series')
        self.assertEqual(result.metadata['station']['status'], 'Not Reporting')
        self.assertIsNone(result.metadata['datum'])
        self.assertTrue(forecast_quality(result.envelope())['unavailable'])
        with self.assertRaises(CollectionError):
            collect_native('tide-gauge', {'station': '../other'}, Source(), 'unused')

    def test_advisory_empty_list_is_explicit_source_result(self):
        class Source:
            def json(self, url):
                return {'HWAJson': '[]', 'SSAJson': '[]', 'LatestHWADate': '20261005'}
        result, _ = collect_native('wave-alerts', {}, Source(), 'unused')
        self.assertEqual(result.data['HWAJson'], [])
        self.assertEqual(result.metadata['publisher_date_labels']['LatestHWADate'], '20261005')

    def test_native_old_observations_remain_stale(self):
        result = Result('incois:waman', 'public', 'source_native', [],
                        {'kind': 'observation', 'retrieved_at': NOW.isoformat(), 'record_issue_times': [{'date': '2024-09-09'}]})
        self.assertTrue(forecast_quality(result.envelope(), now=NOW)['stale'])

    def test_gzip_expansion_is_bounded(self):
        class Response(io.BytesIO):
            headers = {'Content-Encoding': 'gzip'}
        for content, maximum, error in ((b'{}', 100, False), (b'x'*1000, 100, True)):
            with patch('imd_local.core.tls_context'), patch('imd_local.core.urllib.request.urlopen', return_value=Response(gzip.compress(content))):
                if error:
                    with self.assertRaises(CollectionError):
                        Transport(interval=0).request('https://incois.gov.in/example', max_bytes=maximum)
                else:
                    self.assertEqual(Transport(interval=0).request('https://incois.gov.in/example', max_bytes=maximum), content)

    def test_document_redirect_cannot_leave_publisher_hosts(self):
        def opener(*handlers):
            redirect = handlers[-1]
            class Open:
                def open(self, request, timeout):
                    return redirect.redirect_request(request, None, 302, 'redirect', {}, 'https://other.example/document.png')
            return Open()
        with patch('imd_local.core.tls_context'), patch('imd_local.core.urllib.request.build_opener', side_effect=opener):
            with self.assertRaises(CollectionError) as error:
                Transport(interval=0).request('https://incois.gov.in/document.png', allowed_domains=('incois.gov.in',))
            self.assertEqual(error.exception.status, 'invalid_source')

    def test_tide_absence_refused_and_inspection_route_preserved(self):
        with tempfile.TemporaryDirectory() as directory:
            store = Store(Path(directory)/'db.sqlite')
            result = Result('incois:tide-gauge', 'public', 'source_native', [],
                            {'kind': 'observation', 'availability': 'no_series', 'parameters': {},
                             'retrieved_at': datetime.now(timezone.utc).isoformat()})
            store.save(result, {})
            for route, expected in (('/incois/tide-gauge', 503), ('/incois/data/tide-gauge', 200), ('/marine', 200)):
                cls = handler(store, 'public')
                request = cls.__new__(cls)
                request.path, request.wfile = route, io.BytesIO()
                request.send_response = lambda code: setattr(request, 'code', code)
                request.send_header = lambda *args: None
                request.end_headers = lambda: None
                request.do_GET()
                self.assertEqual(request.code, expected)
                payload = json.loads(request.wfile.getvalue())
                if route == '/incois/tide-gauge':
                    self.assertEqual(payload['status'], 'unavailable')
                if route == '/marine':
                    self.assertEqual(payload['components']['tide-gauge']['availability'], 'unavailable')

    def test_marine_view_preserves_components_and_missing_sources(self):
        with tempfile.TemporaryDirectory() as directory:
            store = Store(Path(directory)/'db.sqlite')
            view = marine_view(store, {})
            self.assertTrue(all(r['availability'] == 'missing' for r in view['components'].values()))
            self.assertEqual(view['mode'], 'inspection')
            with self.assertRaises(CollectionError):
                marine_view(store, {'lat': 'nan'})


if __name__ == '__main__':
    unittest.main()
