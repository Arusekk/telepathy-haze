"""
Test text channel being recreated because there are still pending messages.
"""

import dbus

from twisted.words.xish import domish

from hazetest import exec_test
from servicetest import call_async, EventPattern, assertEquals, assertLength
import constants as cs

def test(q, bus, conn, stream):
    self_handle = conn.Properties.Get(cs.CONN, "SelfHandle")

    jid = 'foo@bar.com'
    foo_handle = conn.get_contact_handle_sync(jid)

    call_async(q, conn, 'RequestChannel', cs.CHANNEL_TYPE_TEXT,
            cs.HT_CONTACT, foo_handle, True)

    ret, sig = q.expect_many(
        EventPattern('dbus-return', method='RequestChannel'),
        EventPattern('dbus-signal', signal='NewChannels'),
        )

    text_chan = bus.get_object(conn.bus_name, ret.value[0])
    chan_iface = dbus.Interface(text_chan, cs.CHANNEL)
    text_iface = dbus.Interface(text_chan, cs.CHANNEL_TYPE_TEXT)

    assertLength(1, sig.args)
    assertLength(1, sig.args[0])        # one channel
    assertLength(2, sig.args[0][0])     # two struct members
    assertEquals(ret.value[0], sig.args[0][0][0])
    emitted_props = sig.args[0][0][1]
    assertEquals(cs.CHANNEL_TYPE_TEXT, emitted_props[cs.CHANNEL_TYPE])
    assertEquals(cs.HT_CONTACT, emitted_props[cs.TARGET_HANDLE_TYPE])
    assertEquals(foo_handle, emitted_props[cs.TARGET_HANDLE])
    assertEquals(jid, emitted_props[cs.TARGET_ID])
    assertEquals(True, emitted_props[cs.REQUESTED])
    assertEquals(self_handle, emitted_props[cs.INITIATOR_HANDLE])
    assertEquals('test@localhost', emitted_props[cs.INITIATOR_ID])

    channel_props = text_chan.GetAll(cs.CHANNEL,
            dbus_interface=dbus.PROPERTIES_IFACE)
    assert channel_props['TargetID'] == jid,\
            (channel_props['TargetID'], jid)
    assert channel_props['Requested'] == True
    assert channel_props['InitiatorHandle'] == self_handle,\
            (channel_props['InitiatorHandle'], self_handle)
    assert channel_props['InitiatorID'] == 'test@localhost',\
            channel_props['InitiatorID']

    text_iface.Send(0, 'hey')

    event = q.expect('stream-message')

    elem = event.stanza
    assert elem.name == 'message'
    assert elem['type'] == 'chat'

    found = False
    for e in elem.elements():
        if e.name == 'body':
            found = True
            e.children[0] == u'hey'
            break
    assert found, elem.toXml()

    # <message type="chat"><body>hello</body</message>
    m = domish.Element((None, 'message'))
    m['from'] = 'foo@bar.com/Pidgin'
    m['type'] = 'chat'
    m.addElement('body', content='hello')
    stream.send(m)

    event = q.expect('dbus-signal', signal='Received')

    hello_message_id = event.args[0]
    hello_message_time = event.args[1]
    assert event.args[2] == foo_handle
    # message type: normal
    assert event.args[3] == 0
    # flags: none
    assert event.args[4] == 0
    # body
    assert event.args[5] == 'hello'

    messages = text_chan.ListPendingMessages(False,
            dbus_interface=cs.CHANNEL_TYPE_TEXT)
    assert messages == \
            [(hello_message_id, hello_message_time, foo_handle,
                0, 0, 'hello')], messages

    # close the channel without acking the message; it comes back

    call_async(q, chan_iface, 'Close')

    old, new = q.expect_many(
            EventPattern('dbus-signal', signal='Closed'),
            EventPattern('dbus-signal', signal='ChannelClosed'),
            )
    assertEquals(text_chan.object_path, old.path)
    assertEquals(text_chan.object_path, new.args[0])

    # it now behaves as if the message had initiated it
    new_props = {}
    for k in emitted_props:
        new_props[k] = emitted_props[k]
    new_props[cs.INITIATOR_HANDLE] = foo_handle
    new_props[cs.INITIATOR_ID] = 'foo@bar.com'
    new_props[cs.REQUESTED] = False

    event = q.expect('dbus-signal', signal='NewChannels')
    assertEquals(text_chan.object_path, event.args[0][0][0])
    assertEquals(new_props, event.args[0][0][1])

    event = q.expect('dbus-return', method='Close')

    channel_props = text_chan.GetAll(cs.CHANNEL,
            dbus_interface=dbus.PROPERTIES_IFACE)
    assert channel_props['TargetID'] == jid,\
            (channel_props['TargetID'], jid)
    assert channel_props['Requested'] == False
    assert channel_props['InitiatorHandle'] == foo_handle,\
            (channel_props['InitiatorHandle'], foo_handle)
    assert channel_props['InitiatorID'] == 'foo@bar.com',\
            channel_props['InitiatorID']

    # the message is still there

    messages = text_chan.ListPendingMessages(False,
            dbus_interface=cs.CHANNEL_TYPE_TEXT)
    assert messages == \
            [(hello_message_id, hello_message_time, foo_handle,
                0, 8, 'hello')], messages

    # acknowledge it

    text_chan.AcknowledgePendingMessages([hello_message_id],
            dbus_interface=cs.CHANNEL_TYPE_TEXT)

    messages = text_chan.ListPendingMessages(False,
            dbus_interface=cs.CHANNEL_TYPE_TEXT)
    assert messages == []

    # close the channel again

    call_async(q, chan_iface, 'Close')

    event = q.expect('dbus-signal', signal='Closed')
    assertEquals(text_chan.object_path, event.path)

    event = q.expect('dbus-return', method='Close')

    # assert that it stays dead this time!

    try:
        chan_iface.GetChannelType()
    except dbus.DBusException:
        pass
    else:
        raise AssertionError("Why won't it die?")

    conn.Disconnect()
    q.expect('dbus-signal', signal='StatusChanged', args=[2, 1])

if __name__ == '__main__':
    exec_test(test)

