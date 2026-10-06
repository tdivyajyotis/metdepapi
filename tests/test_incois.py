import io
import json
import tempfile
import unittest
from datetime import datetime, timedelta, timezone
from pathlib import Path
from urllib.parse import parse_qs, urlsplit

from imd_local.core import CollectionError, Store, Result
from imd_local.incois import (PRODUCTS, collect_incois, time_axis, latest_dataset,
                              layer_metadata, point_value, forecast_quality)
from imd_local.cli import handler
from imd_local.operations import run_job


NOW = datetime(2026, 10, 5, tzinfo=timezone.utc)


class Source:
    """Synthetic service behavior; these responses are not a live capture."""
    def __init__(self, fail=False, missing=False):
        self.fail, self.missing = fail, missing
        self.calls = []

    def text(self, url):
        self.calls.append(url)
        if 'catalog.xml' in url:
            return '<catalog><dataset urlPath="osf/ww3/rsmc_combined_ww3_20261004.nc"><date type="modified">2026-10-05T00:00:00Z</date></dataset></catalog>'
        if 'GetCapabilities' in url:
            return '<WMS_Capabilities><Layer><Dimension name="time">2026-10-05T00:00:00Z/2026-10-05T09:00:00Z/PT3H</Dimension><Layer><Name>HS</Name><Title>Wave height (m)</Title></Layer><Layer><Name>T02</Name><Title>Mean period (s)</Title></Layer></Layer></WMS_Capabilities>'
        if url.endswith('.das'):
            return 'Attributes {\n HS { Float64 _FillValue -1.0E34; String long_name "Wave height (m)"; }\n T02 { String units "s"; }\n}'
        query = parse_qs(urlsplit(url).query)
        name = query['layers'][0]
        if self.fail and name == 'T02':
            raise CollectionError('network_error', 'Unavailable')
        value = '-1.0E34' if self.missing and name == 'HS' else '1.5'
        return f'Clicked:\n Longitude: 84.94\n Latitude: 19.24\nLayer: {name}\nTime: {query["time"][0]}\nValue: {value}\n'


class IncoisTests(unittest.TestCase):
    def test_time_axis_bounds_and_timezone(self):
        self.assertEqual(len(time_axis('2026-10-05T00:00:00Z/2026-10-05T09:00:00Z/PT3H')), 4)
        for bad in ('2026-10-05T00:00:00', '2026-10-05T00:00:00Z/2027-10-05T00:00:00Z/PT1S',
                    '2026-10-05T00:00:00Z/2026-10-06T00:00:00Z/PT0H'):
            with self.assertRaises(CollectionError):
                time_axis(bad)

    def test_dataset_selection_ignores_unrelated_and_unsafe_paths(self):
        text = '<catalog><dataset urlPath="https://evil.test/rsmc_combined_ww3_20990101.nc"/><dataset urlPath="osf/ww3/rsmc_combined_ww3_20261004.nc"/><dataset urlPath="osf/ww3/rsmc_combined_ww3_20260930.nc"/></catalog>'
        self.assertEqual(latest_dataset(text, PRODUCTS['lsf-wave'])[0], '20261004')

    def test_surface_dimensions_are_inherited(self):
        value = '<Layer><Dimension name="time">2026-10-05T00:00:00Z</Dimension><Layer><Name>UVEL</Name><Dimension name="elevation">0.0,10.0</Dimension></Layer></Layer>'
        self.assertEqual(layer_metadata(value)['UVEL']['dimensions']['elevation'], '0.0,10.0')
        self.assertIn('time', layer_metadata(value)['UVEL']['dimensions'])

    def test_missing_values_and_response_identity(self):
        text = 'Longitude: 84.94\nLatitude: 19.24\nLayer: HS\nTime: 2026-10-05T00:00:00Z\nValue: -1.0E34\n'
        self.assertIsNone(point_value(text, 'HS', NOW, 19.24, 84.94, [-1e34])[0])
        with self.assertRaises(CollectionError):
            point_value(text, 'SSH', NOW, 19.24, 84.94, [])
        with self.assertRaises(CollectionError):
            point_value('<html>login</html>', 'HS', NOW, 19.24, 84.94, [])

    def test_collection_preserves_unknowns_and_isolates_failure(self):
        result, raw = collect_incois('lsf-wave', {'lat': '19.24', 'lon': '84.94', 'steps': '2'}, Source(fail=True), now=NOW)
        self.assertEqual(result.status, 'partial')
        self.assertEqual(len(result.data), 4)
        self.assertEqual(result.data[0]['units'], 'm')
        self.assertEqual(result.data[0]['unit_basis'], 'DAS.long_name')
        self.assertIsNone(result.metadata['issue_time'])
        self.assertIsNone(result.metadata['sampling']['grid_cell_coordinates'])
        self.assertEqual(result.metadata['publisher'], 'INCOIS')
        self.assertTrue(raw)

    def test_invalid_parameters_and_all_missing(self):
        for params in ({'lat': 'nan', 'lon': '84.94'}, {'lat': '19.24', 'lon': '84.94', 'steps': '57'},
                       {'lat': '19.24', 'lon': '84.94', 'layers': 'unknown'}):
            with self.assertRaises(CollectionError):
                collect_incois('lsf-wave', params, Source(), now=NOW)
        with self.assertRaises(CollectionError):
            collect_incois('lsf-wave', {'lat': '19.24', 'lon': '84.94', 'layers': 'HS'}, Source(missing=True), now=NOW)

    def test_future_forecast_and_expired_snapshot_routes(self):
        now = datetime.now(timezone.utc)
        result = Result('incois:lsf-wave', 'public', 'source_native',
                        [{'valid_time': (now-timedelta(hours=1)).isoformat(), 'value': 1}],
                        {'parameters': {}, 'retrieved_at': now.isoformat()})
        self.assertTrue(forecast_quality(result.envelope())['forecast_expired'])
        result.data[0]['valid_time'] = (now+timedelta(hours=2)).isoformat()
        self.assertFalse(forecast_quality(result.envelope())['stale'])
        result.data[0]['valid_time'] = (now-timedelta(hours=1)).isoformat()
        with tempfile.TemporaryDirectory() as directory:
            store = Store(Path(directory)/'snapshot.sqlite')
            store.save(result, {})
            for route, expected in (('/incois/lsf-wave', 503), ('/incois/data/lsf-wave', 200),
                                    ('/incois/lsf-wave?lat=19.24', 503), ('/incois/coverage', 200)):
                cls = handler(store, 'public')
                request = cls.__new__(cls)
                request.path, request.wfile = route, io.BytesIO()
                request.send_response = lambda code: setattr(request, 'code', code)
                request.send_header = lambda *args: None
                request.end_headers = lambda: None
                request.do_GET()
                self.assertEqual(request.code, expected)

    def test_job_failure_preserves_good_snapshot(self):
        with tempfile.TemporaryDirectory() as directory:
            store = Store(Path(directory)/'snapshot.sqlite')
            params = {'lat': '19.24', 'lon': '84.94'}
            old = Result('incois:lsf-wave', 'public', 'source_native', [],
                         {'parameters': params, 'retrieved_at': NOW.isoformat()})
            store.save(old, {})
            job = {'product': 'incois:lsf-wave', 'provider': 'official', 'params': params}
            self.assertTrue(run_job(job, store, Source())['failed'])
            self.assertEqual(store.latest('incois:lsf-wave', 'public', params)['metadata']['retrieved_at'], NOW.isoformat())


if __name__ == '__main__':
    unittest.main()
