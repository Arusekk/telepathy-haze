"""
Regression tests for rescinding outstanding subscription requests.
"""

from twisted.words.protocols.jabber.client import IQ

from servicetest import (EventPattern, wrap_channel, assertLength,
        assertEquals, call_async, sync_dbus)
from hazetest import exec_test
import constants as cs
import ns

jid = 'marco@barisione.lit'

def test(q, bus, conn, stream, remove, local):
    h = conn.get_contact_handle_sync(jid)

    # Another client logged into our account (Gajim, say) wants to subscribe to
    # Marco's presence. First, per RFC 3921 it 'SHOULD perform a "roster set"
    # for the new roster item':
    #
    #   <iq type='set'>
    #     <query xmlns='jabber:iq:roster'>
    #       <item jid='marco@barisione.lit'/>
    #     </query>
    #   </iq>
    #
    # 'As a result, the user's server (1) MUST initiate a roster push for the
    # new roster item to all available resources associated with this user that
    # have requested the roster, setting the 'subscription' attribute to a
    # value of "none"':
    iq = IQ(stream, "set")
    item = iq.addElement((ns.ROSTER, 'query')).addElement('item')
    item['jid'] = jid
    item['subscription'] = 'none'
    stream.send(iq)

    # In response, Haze adds Marco to the roster, which we guess (wrongly,
    # in this case) also means subscribe
    q.expect('dbus-signal', signal='ContactsChangedWithID',
            args=[{
                h:
                    (cs.SUBSCRIPTION_STATE_YES,
                        cs.SUBSCRIPTION_STATE_UNKNOWN, ''),
                },
                {h: jid}, {}])

    # Gajim sends a <presence type='subscribe'/> to Marco. 'As a result, the
    # user's server MUST initiate a second roster push to all of the user's
    # available resources that have requested the roster, setting [...]
    # ask='subscribe' attribute in the roster item [for Marco]:
    iq = IQ(stream, "set")
    item = iq.addElement((ns.ROSTER, 'query')).addElement('item')
    item['jid'] = jid
    item['subscription'] = 'none'
    item['ask'] = 'subscribe'
    stream.send(iq)

    # In response, Haze should add Marco to subscribe:remote-pending,
    # but libpurple has no such concept, so nothing much happens.

    # The user decides that they don't care what Marco's baking after all
    # (maybe they read his blog instead?) and:
    if remove:
        # ...removes him from the roster...
        if local:
            # ...by telling Haze to remove him from the roster
            conn.ContactList.RemoveContacts([h])

            event = q.expect('stream-iq', iq_type='set', query_ns=ns.ROSTER)
            item = event.query.firstChildElement()
            assertEquals(jid, item['jid'])
            assertEquals('remove', item['subscription'])
        else:
            # ...using the other client.
            pass

        # The server must 'inform all of the user's available resources that
        # have requested the roster of the roster item removal':
        iq = IQ(stream, "set")
        item = iq.addElement((ns.ROSTER, 'query')).addElement('item')
        item['jid'] = jid
        item['subscription'] = 'remove'
        # When Marco found this bug, this roster update included:
        item['ask'] = 'subscribe'
        # which is a bit weird: I don't think the server should send that when
        # the contact's being removed. I think CMs should ignore it, so I'm
        # including it in the test.
        stream.send(iq)

        # In response, Haze should announce that Marco has been removed from
        # the roster
        q.expect('dbus-signal', signal='ContactsChangedWithID',
                args=[{}, {}, {h: jid}])
    else:
        # ...rescinds the subscription request...
        if local:
            raise AssertionError("Haze can't do this ")
        else:
            # ...in the other client.
            pass

        # In response, the server sends a roster update:
        iq = IQ(stream, "set")
        item = iq.addElement((ns.ROSTER, 'query')).addElement('item')
        item['jid'] = jid
        item['subscription'] = 'none'
        # no ask='subscribe' any more.
        stream.send(iq)

        # In response, Haze should announce that Marco has been removed from
        # subscribe:remote-pending; but it can't know that, so nothing happens.

def test_remove_local(q, bus, conn, stream):
    test(q, bus, conn, stream, remove=True, local=True)

def test_remove_remote(q, bus, conn, stream):
    test(q, bus, conn, stream, remove=True, local=False)

def test_unsubscribe_remote(q, bus, conn, stream):
    test(q, bus, conn, stream, remove=False, local=False)

if __name__ == '__main__':
    exec_test(test_remove_local)
    exec_test(test_remove_remote)
    exec_test(test_unsubscribe_remote)
