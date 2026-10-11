// SolarSense dashboard CI: Selenium functional test + HTTP performance test.
//
// Agent requirements: Node.js 18+, Python 3.9+, Google Chrome (or Edge), and
// internet access (npm/pip registries, the dashboard's CDN scripts, and
// Selenium Manager's driver download). Works on Linux and Windows agents.

def shell(String cmd) { if (isUnix()) { sh cmd } else { bat cmd } }
def venvPython() { return isUnix() ? '.venv/bin/python' : '.venv\\Scripts\\python.exe' }

pipeline {
    agent any

    options {
        timeout(time: 20, unit: 'MINUTES')
        buildDiscarder(logRotator(numToKeepStr: '20'))
    }

    parameters {
        booleanParam(name: 'START_APP', defaultValue: true, description: 'Start dashboard/server.js on this agent. Untick to test an already-running instance at SOLARSENSE_URL.')
        string(name: 'APP_PORT', defaultValue: '3000', description: 'Port for the local server (used when START_APP is ticked)')
        string(name: 'SOLARSENSE_URL', defaultValue: '', description: 'Target URL when START_APP is unticked, e.g. http://my-host:3000')
        choice(name: 'SELENIUM_BROWSER', choices: ['chrome', 'edge'], description: 'Browser for the functional test')
        string(name: 'PERF_CONCURRENCY', defaultValue: '5', description: 'Concurrent workers (1-50)')
        string(name: 'PERF_REQUESTS', defaultValue: '100', description: 'Requests per endpoint (1-2000)')
        string(name: 'PERF_MAX_AVG_MS', defaultValue: '200', description: 'Max average response time (ms)')
        string(name: 'PERF_MAX_P95_MS', defaultValue: '500', description: 'Max 95th percentile response time (ms)')
        string(name: 'PERF_MAX_MS', defaultValue: '2000', description: 'Max single response time (ms)')
        string(name: 'PERF_MIN_SUCCESS_RATE', defaultValue: '0.99', description: 'Minimum fraction of 2xx responses')
    }

    environment {
        HEADLESS = '1'
        PIP_DISABLE_PIP_VERSION_CHECK = '1'
    }

    stages {
        stage('Install dependencies') {
            steps {
                dir('dashboard') { shell 'npm ci --no-audit --no-fund' }
                shell "${isUnix() ? 'python3' : 'python'} -m venv .venv"
                shell "${venvPython()} -m pip install -r tests/requirements.txt"
            }
        }

        stage('Start application') {
            when { expression { params.START_APP } }
            steps {
                script {
                    env.SOLARSENSE_URL = "http://localhost:${params.APP_PORT}"
                    // If something already listens on the port, the new server would crash and the
                    // tests would silently run against that other process instead. Fail clearly.
                    shell "${venvPython()} -c \"import socket,sys; p=int(sys.argv[1]); s=socket.socket(); busy=s.connect_ex(('127.0.0.1',p))==0; print(f'Port {p} is already in use - stop that process or choose another APP_PORT.' if busy else f'Port {p} is free.'); sys.exit(1 if busy else 0)\" ${params.APP_PORT}"
                    // Run `node server.js` (what `npm start` runs) and save its PID (on Windows, its cmd wrapper).
                    // JENKINS_NODE_COOKIE/BUILD_ID stop Jenkins killing it when this step ends;
                    // the post block below stops it explicitly.
                    if (isUnix()) {
                        sh '''
                            mkdir -p reports
                            cd dashboard
                            JENKINS_NODE_COOKIE=dontKillMe BUILD_ID=dontKillMe PORT="$APP_PORT" \
                                nohup node server.js > ../reports/server.log 2>&1 &
                            echo $! > ../app.pid
                        '''
                    } else {
                        powershell '''
                            New-Item -ItemType Directory -Force reports | Out-Null
                            $env:JENKINS_NODE_COOKIE = 'dontKillMe'
                            $env:BUILD_ID = 'dontKillMe'
                            $env:PORT = $env:APP_PORT
                            # No -NoNewWindow / -Redirect*: those make node inherit this step's output
                            # handle and the step never finishes. cmd does the redirection instead.
                            $p = Start-Process cmd.exe -WorkingDirectory dashboard -WindowStyle Hidden -PassThru `
                                -ArgumentList '/c', 'node server.js > ..\\reports\\server.log 2> ..\\reports\\server.err.log'
                            $p.Id | Out-File -Encoding ascii app.pid
                        '''
                    }
                }
            }
        }

        stage('Wait until ready') {
            steps {
                script {
                    if (!env.SOLARSENSE_URL?.trim()) { error 'Set SOLARSENSE_URL when START_APP is unticked.' }
                }
                shell "${venvPython()} tests/wait_for_app.py ${env.SOLARSENSE_URL} 60"
            }
        }

        stage('Functional test (Selenium)') {
            steps {
                // A failing test marks the build UNSTABLE but still lets the performance test run.
                catchError(buildResult: 'UNSTABLE', stageResult: 'UNSTABLE') {
                    shell "${venvPython()} -m pytest tests/functional -v --junitxml=reports/junit-functional.xml"
                }
            }
        }

        stage('Performance test (HTTP)') {
            steps {
                catchError(buildResult: 'UNSTABLE', stageResult: 'UNSTABLE') {
                    shell "${venvPython()} -m pytest tests/performance -v -s --junitxml=reports/junit-performance.xml"
                }
            }
        }
    }

    post {
        always {
            junit allowEmptyResults: true, testResults: 'reports/junit-*.xml'
            archiveArtifacts allowEmptyArchive: true, artifacts: 'reports/**'
            script {
                if (fileExists('app.pid')) {
                    if (isUnix()) {
                        sh 'kill "$(cat app.pid)" 2>/dev/null || true; rm -f app.pid'
                    } else {
                        powershell '''
                            # app.pid is the cmd wrapper; stop its node child first, then cmd.
                            $appPid = [int](Get-Content app.pid)
                            Get-CimInstance Win32_Process -Filter "ParentProcessId=$appPid" |
                                ForEach-Object { Stop-Process -Id $_.ProcessId -Force -ErrorAction SilentlyContinue }
                            Stop-Process -Id $appPid -Force -ErrorAction SilentlyContinue
                            Remove-Item app.pid -ErrorAction SilentlyContinue
                        '''
                    }
                }
            }
        }
    }
}
