param(
    [string]$BaseUrl = "https://dashboard.turtleguard.in",
    [string]$DeviceId = "station-001",
    [int]$Limit = 500
)

$ErrorActionPreference = "Stop"
$ProgressPreference = "SilentlyContinue"

function Get-Api([string]$Path) {
    Invoke-RestMethod -Uri "$BaseUrl$Path" -Method Get -TimeoutSec 60
}

$encodedDevice = [Uri]::EscapeDataString($DeviceId)
$response = Get-Api "/v1/readings?device_id=$encodedDevice&limit=$Limit"
$rows = @($response.items | Sort-Object { [DateTimeOffset]$_.received_at })

$samples = @(
    foreach ($row in $rows) {
        $clock = $row.telemetry.timekeeping
        $sensorClock = $row.sensors.timekeeping
        $gps = $row.sensors.gps
        [pscustomobject]@{
            event_id = [string]$row.event_id
            boot_id = [string]$row.telemetry.nodemcu.boot_id
            sequence = [int64]$row.sequence
            uptime_ms = [int64]$row.uptime_ms
            received = [DateTimeOffset]$row.received_at
            observed = if ($null -ne $row.observed_at) {
                [DateTimeOffset]$row.observed_at
            } else { $null }
            source = [string]$clock.source
            system_epoch = if ($null -ne $clock.system_epoch) {
                [int64]$clock.system_epoch
            } elseif ($null -ne $sensorClock.system_epoch) {
                [int64]$sensorClock.system_epoch
            } else { $null }
            gps_epoch = if ($null -ne $clock.gps_epoch) {
                [int64]$clock.gps_epoch
            } else { $null }
            gps_time_valid = $gps.time_valid -eq $true
            gps_fix_valid = $gps.fix_valid -eq $true
            gps_present = $row.telemetry.gps_uart.receiving_recently -eq $true
            gps_last_rx_age_ms = if ($null -ne $row.telemetry.gps_uart.last_rx_age_ms) {
                [int64]$row.telemetry.gps_uart.last_rx_age_ms
            } else { $null }
            stored_offline = $row.telemetry.delivery.stored_offline -eq $true
        }
    }
)

$withinBoot = @(
    for ($index = 1; $index -lt $samples.Count; $index++) {
        $previous = $samples[$index - 1]
        $current = $samples[$index]
        if ($previous.boot_id -eq $current.boot_id -and
            $current.uptime_ms -gt $previous.uptime_ms) {
            $uptimeDelta = ($current.uptime_ms - $previous.uptime_ms) / 1000.0
            $systemDelta = if ($null -ne $previous.system_epoch -and
                $null -ne $current.system_epoch) {
                $current.system_epoch - $previous.system_epoch
            } else { $null }
            [pscustomobject]@{
                from = $previous.event_id
                to = $current.event_id
                uptime_delta_s = $uptimeDelta
                system_delta_s = $systemDelta
                error_s = if ($null -ne $systemDelta) {
                    $systemDelta - $uptimeDelta
                } else { $null }
            }
        }
    }
)

$systemGps = @($samples | Where-Object {
    $null -ne $_.system_epoch -and $null -ne $_.gps_epoch
})
$rateAnomalies = @($withinBoot | Where-Object {
    $null -ne $_.error_s -and [math]::Abs($_.error_s) -gt 5
})

$summary = [ordered]@{
    device_id = $DeviceId
    readings = $samples.Count
    first_received = if ($samples.Count) { $samples[0].received } else { $null }
    last_received = if ($samples.Count) { $samples[-1].received } else { $null }
    boots = @($samples.boot_id | Where-Object { $_ } | Sort-Object -Unique).Count
    clock_sources = @($samples | Group-Object source | ForEach-Object {
        [pscustomobject]@{ source = $_.Name; count = $_.Count }
    })
    gps_present_readings = @($samples | Where-Object { $_.gps_present }).Count
    gps_time_valid_readings = @($samples | Where-Object { $_.gps_time_valid }).Count
    gps_fix_valid_readings = @($samples | Where-Object { $_.gps_fix_valid }).Count
    gps_receive_age_max_ms = if ($samples.Count) {
        ($samples.gps_last_rx_age_ms | Where-Object { $null -ne $_ } |
            Measure-Object -Maximum).Maximum
    } else { $null }
    offline_replays = @($samples | Where-Object { $_.stored_offline }).Count
    system_gps_pairs = $systemGps.Count
    system_gps_max_abs_s = if ($systemGps.Count) {
        ($systemGps | ForEach-Object {
            [math]::Abs($_.system_epoch - $_.gps_epoch)
        } | Measure-Object -Maximum).Maximum
    } else { $null }
    system_gps_over_2s = @($systemGps | Where-Object {
        [math]::Abs($_.system_epoch - $_.gps_epoch) -gt 2
    }).Count
    within_boot_rate_pairs = $withinBoot.Count
    system_rate_anomalies_over_5s = $rateAnomalies.Count
    largest_system_gps_offsets = @($systemGps |
        Sort-Object { [math]::Abs($_.system_epoch - $_.gps_epoch) } -Descending |
        Select-Object -First 12 event_id, received, source, system_epoch, gps_epoch)
    largest_system_rate_errors = @($rateAnomalies |
        Sort-Object { [math]::Abs($_.error_s) } -Descending |
        Select-Object -First 12)
}

$summary | ConvertTo-Json -Depth 8
