"""Functional test: the SolarSense dashboard loads and renders live telemetry.

dashboard/index.html ships with hard-coded placeholder numbers (e.g. "22.84 W",
"#0" sequence) that updateDashboard() overwrites after fetching
/api/sensors/latest and /api/sensors/history. Checking that an element merely
exists would pass even if the JavaScript never ran, so this test waits for the
placeholders to be replaced and then cross-checks the rendered values against
the same API the page reads from.
"""

import os

import pytest
import requests
from selenium.webdriver.common.by import By
from selenium.webdriver.support import expected_conditions as EC
from selenium.webdriver.support.ui import WebDriverWait

TIMEOUT_S = int(os.environ.get("SELENIUM_TIMEOUT_S", "20"))

# IDs taken from dashboard/index.html.
METRIC_CARD_IDS = [
    "cardLuxValue", "luxStatusText", "sunSuitabilityText",
    "cardPanelAPower", "cardPanelAVolts", "cardPanelAAmps", "cardPanelATemp",
    "cardPanelBPower", "cardPanelBVolts", "cardPanelBAmps", "cardPanelBTemp",
    "cardPanelCPower", "cardPanelCVolts", "cardPanelCAmps", "cardPanelCTemp",
]
HEADER_IDS = ["headerNodeId", "headerTimestamp", "headerTimeSource", "headerSeq"]
AI_PANEL_IDS = ["heroRecTitle", "targetZoneBadge", "btnActuate", "armStatusText"]
CHART_IDS = ["chartPower", "chartLossLux"]
ZONE_GRID_IDS = ["gridPanelA", "gridPanelB", "gridPanelC"]
ZONES_PER_PANEL = 12  # server.js generateZoneData(): 4 columns x 3 rows

# Values the server can emit for light_status (server.js createTelemetryRecord()).
VALID_LIGHT_STATUS = {"OK", "SATURATED", "LOW_LIGHT"}
VALID_ARM_STATUS = {"IDLE", "MOVING", "CLEANING", "VERIFYING"}

# Read everything in one script so the 2.5 s polling loop cannot update the
# page between reading the sequence number and reading the power values.
SNAPSHOT_JS = """
const t = id => document.getElementById(id).textContent.trim();
return {
  seq: t('headerSeq'), nodeId: t('headerNodeId'), lux: t('cardLuxValue'),
  luxStatus: t('luxStatusText'), arm: t('armStatusText'),
  powerA: t('cardPanelAPower'), powerB: t('cardPanelBPower'), powerC: t('cardPanelCPower'),
};
"""


def _leading_float(text):
    """'22.84 W' -> 22.84, '84,512.3 Lux' -> 84512.3"""
    return float(text.split()[0].replace(",", ""))


@pytest.mark.functional
def test_dashboard_loads_with_live_panel_readings(driver, base_url):
    wait = WebDriverWait(driver, TIMEOUT_S)

    # 1. Page loads.
    driver.get(base_url + "/")
    wait.until(EC.title_contains("SolarSense"))
    brand = wait.until(EC.visibility_of_element_located((By.TAG_NAME, "h1")))
    assert brand.text == "SolarSense"

    # 2. Live data replaced the static placeholder ("#0" is the HTML default).
    #    If the CDN scripts (Tailwind / Chart.js / Lucide) fail to load, the
    #    dashboard script aborts and this wait times out.
    try:
        wait.until(lambda d: d.find_element(By.ID, "headerSeq").text not in ("", "#0"))
    except Exception:
        pytest.fail("Dashboard never rendered live telemetry: #headerSeq stayed at its "
                    "placeholder. Check the server is running and the CDN scripts loaded.")

    # 3. The zone heatmaps and telemetry table are built by JS from API data.
    for grid_id in ZONE_GRID_IDS:
        wait.until(lambda d, g=grid_id: len(d.find_elements(By.CSS_SELECTOR, f"#{g} > div")) == ZONES_PER_PANEL,
                   message=f"#{grid_id} did not render {ZONES_PER_PANEL} zones")
    wait.until(lambda d: len(d.find_elements(By.CSS_SELECTOR, "#telemetryTableBody tr")) > 0,
               message="telemetry table stayed empty")

    # 4. Key dashboard elements are visible.
    for element_id in HEADER_IDS + METRIC_CARD_IDS + AI_PANEL_IDS + CHART_IDS + ZONE_GRID_IDS:
        el = driver.find_element(By.ID, element_id)
        assert el.is_displayed(), f"#{element_id} is not visible"
    assert driver.find_element(By.ID, "btnActuate").is_enabled()

    # 5. Rendered readings match the record the server returned for that sequence number.
    snap = driver.execute_script(SNAPSHOT_JS)
    seq = int(snap["seq"].lstrip("#"))
    history = requests.get(base_url + "/api/sensors/history", timeout=10).json()
    record = next((r for r in history if r.get("seq") == seq), None)
    assert record is not None, f"seq #{seq} shown on the dashboard is not in /api/sensors/history"

    assert snap["nodeId"] == record["device_id"]
    assert snap["luxStatus"] == record["light_status"]
    assert snap["luxStatus"] in VALID_LIGHT_STATUS
    assert _leading_float(snap["lux"]) == pytest.approx(record["light_lux"], abs=0.05)
    for panel in ("A", "B", "C"):
        shown = _leading_float(snap[f"power{panel}"])
        expected = record[f"panel{panel}"]["power"]
        assert shown == pytest.approx(expected, abs=0.005), \
            f"Panel {panel} power shows {shown} W, API says {expected} W"
        assert shown >= 0

    arm_status = snap["arm"].removeprefix("Robotic Arm Status:").strip()
    assert arm_status in VALID_ARM_STATUS, f"unexpected robotic arm status {arm_status!r}"

    # 6. No JavaScript errors from the app itself (favicon 404 is harmless:
    #    the repo ships no favicon). Only Chromium-based drivers expose this log.
    try:
        logs = driver.get_log("browser")
    except Exception:
        logs = []
    errors = [e["message"] for e in logs
              if e.get("level") == "SEVERE" and "favicon.ico" not in e.get("message", "")]
    assert not errors, "Browser console errors:\n" + "\n".join(errors)
