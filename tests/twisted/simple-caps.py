
"""
Make sure ContactCaps works well enough.
"""

from twisted.words.xish import domish

from servicetest import assertEquals, assertContains, EventPattern
from hazetest import exec_test, sync_stream, JabberXmlStream
import constants as cs

import ns

# assert this list of RCCs is only text
def check_text_only(rccs):
    assertEquals([({
                cs.CHANNEL_TYPE: cs.CHANNEL_TYPE_TEXT,
                cs.TARGET_HANDLE_TYPE: cs.HT_CONTACT
                }, [cs.TARGET_HANDLE])], rccs)

# assert just text caps
def check_rccs(conn, handle):
    attrs = conn.Contacts.GetContactAttributes([handle],
                                               [cs.CONN_IFACE_CONTACT_CAPS],
                                               False)
    rccs = attrs[handle][cs.CONN_IFACE_CONTACT_CAPS + '/capabilities']
    check_text_only(rccs)

# do the self handle which will just be text
def test_self_handle(q, bus, conn, stream):
    self_handle = conn.Properties.Get(cs.CONN, 'SelfHandle')

    check_rccs(conn, self_handle)

# do someone else which will also just be text
def test_someone_else(q, bus, conn, stream):
    conn.Connect()
    q.expect_many(
            EventPattern('dbus-signal', signal='StatusChanged',
                args=[cs.CONN_STATUS_CONNECTING, cs.CSR_REQUESTED]),
            EventPattern('stream-authenticated'),
            )

    event = q.expect('stream-iq', query_ns=ns.ROSTER)
    event.stanza['type'] = 'result'

    item = event.query.addElement('item')
    item['jid'] = 'bob@foo.com'
    item['subscription'] = 'both'

    stream.send(event.stanza)

    q.expect('dbus-signal', signal='StatusChanged',
            args=[cs.CONN_STATUS_CONNECTED, cs.CSR_REQUESTED])

    bob_handle = conn.RequestHandles(cs.HT_CONTACT, ['bob@foo.com'])[0]
    check_rccs(conn, bob_handle)

    # now a randomer who isn't even in our contact list
    amy_handle = conn.RequestHandles(cs.HT_CONTACT, ['amy@foo.com'])[0]
    check_rccs(conn, amy_handle)

if __name__ == '__main__':
    exec_test(test_self_handle)
    exec_test(test_someone_else, do_connect=False, protocol=JabberXmlStream)
