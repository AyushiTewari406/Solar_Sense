import os
import pathlib

import pytest

REPO_ROOT = pathlib.Path(__file__).resolve().parent.parent


def env_str(name, default):
    return os.environ.get(name, default)


def env_float(name, default):
    return float(os.environ.get(name, default))


def env_int(name, default):
    return int(os.environ.get(name, default))


@pytest.fixture(scope="session")
def base_url():
    return env_str("SOLARSENSE_URL", "http://localhost:3000").rstrip("/")


@pytest.fixture(scope="session")
def reports_dir():
    path = pathlib.Path(env_str("REPORTS_DIR", str(REPO_ROOT / "reports")))
    path.mkdir(parents=True, exist_ok=True)
    return path


@pytest.fixture
def driver(reports_dir):
    from selenium import webdriver

    browser = env_str("SELENIUM_BROWSER", "chrome").lower()
    headless = env_str("HEADLESS", "1") != "0"

    if browser == "edge":
        options = webdriver.EdgeOptions()
    elif browser == "chrome":
        options = webdriver.ChromeOptions()
    else:
        raise pytest.UsageError(f"Unsupported SELENIUM_BROWSER={browser!r} (use chrome or edge)")

    if headless:
        options.add_argument("--headless=new")
    options.add_argument("--window-size=1440,1000")
    options.add_argument("--no-sandbox")
    options.add_argument("--disable-dev-shm-usage")
    options.set_capability("goog:loggingPrefs", {"browser": "ALL"})

    drv = webdriver.Edge(options=options) if browser == "edge" else webdriver.Chrome(options=options)
    drv.set_page_load_timeout(env_int("SELENIUM_TIMEOUT_S", 20) * 2)
    yield drv
    drv.quit()


@pytest.hookimpl(hookwrapper=True)
def pytest_runtest_makereport(item, call):
    outcome = yield
    report = outcome.get_result()
    if report.when != "call" or not report.failed:
        return
    drv = item.funcargs.get("driver")
    if drv is None:
        return
    shots = pathlib.Path(env_str("REPORTS_DIR", str(REPO_ROOT / "reports"))) / "screenshots"
    shots.mkdir(parents=True, exist_ok=True)
    stem = item.nodeid.replace("/", "_").replace("::", "__").replace(".py", "")
    png = shots / f"{stem}.png"
    try:
        drv.save_screenshot(str(png))
        (shots / f"{stem}.html").write_text(drv.page_source, encoding="utf-8")
        report.sections.append(("failure screenshot", str(png)))
        print(f"\n[screenshot saved] {png}")
    except Exception as exc:
        print(f"\n[screenshot failed] {exc}")
