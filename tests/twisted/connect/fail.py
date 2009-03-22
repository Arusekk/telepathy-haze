
"""
Test network error handling.
"""

import dbus

from hazetest import exec_test

def test(q, bus, conn, stream):
    conn.Connect()
    q.expect('dbus-signal', signal='StatusChanged', args=[1, 1])
    q.expect('dbus-signal', signal='StatusChanged', args=[2, 2])

if __name__ == '__main__':
    exec_test(test, {'port': dbus.Int32(4243)})
