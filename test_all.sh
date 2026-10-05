#!/bin/bash
# Comprehensive RemoteOps test script - IT24103002
# Runs controller with scripted input and shows all responses.

set -e

# Prepare test file
echo -n "Hello from controller" > /tmp/test.txt

# Feed commands to controller, one per line
./controller_002 <<EOF
sysinfo
listproc
exec DATE
exec UPTIME
exec DISKFREE
exec HOSTNAME
exec WHOAMI
exec rm -rf /
exec cat /etc/passwd
put /tmp/test.txt
get test.txt /tmp/downloaded.txt
quit
EOF

echo ""
echo "=== Verifying downloaded file ==="
if cmp -s /tmp/test.txt /tmp/downloaded.txt; then
    echo "BYTE-IDENTICAL ✅"
else
    echo "DIFFERENT ❌"
fi

echo ""
echo "=== Agent storage ==="
ls -la agentfiles/IT24103002/
