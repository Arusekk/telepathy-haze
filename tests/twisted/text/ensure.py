"""
Test text channel initiated by me, using Requests.EnsureChannel
"""

import dbus

from twisted.words.xish import domish

from hazetest import exec_test
from servicetest import call_async, EventPattern, unwrap

import pprint

import constants as cs

def test(q, bus, conn, stream):
    conn.Connect()
    q.expect('dbus-signal', signal='StatusChanged', args=[0, 1])

    self_handle = conn.Properties.Get(cs.CONN, 'SelfHandle')

    jids = ['foo@bar.com', 'truc@cafe.fr']
    call_async(q, conn, 'RequestHandles', 1, jids)

    event = q.expect('dbus-return', method='RequestHandles')
    handles = event.value[0]

    properties = conn.GetAll(
            'im.telepathy1.Connection.Interface.Requests',
            dbus_interface=dbus.PROPERTIES_IFACE)
    # Difference from Gabble: Haze's roster channels spring to life even if you
    # haven't received the XMPP roster.
    text_channels = [c for c in properties.get('Channels')
                if c[1][cs.CHANNEL_TYPE] == cs.CHANNEL_TYPE_TEXT
               ]
    assert text_channels == [], text_channels
    assert ({'im.telepathy1.Channel.ChannelType':
                'im.telepathy1.Channel.Type.Text',
             'im.telepathy1.Channel.TargetHandleType': 1,
             },
             ['im.telepathy1.Channel.TargetHandle',
              'im.telepathy1.Channel.TargetID'
             ],
             ) in properties.get('RequestableChannelClasses'),\
                     properties['RequestableChannelClasses']

    test_ensure_ensure(q, conn, self_handle, jids[0], handles[0])
    test_request_ensure(q, conn, self_handle, jids[1], handles[1])

    conn.Disconnect()
    q.expect('dbus-signal', signal='StatusChanged', args=[2, 1])


def test_ensure_ensure(q, conn, self_handle, jid, handle):
    """
    Test ensuring a non-existant channel twice.  The first call should succeed
    with Yours=True; the subsequent call should succeed with Yours=False
    """

    # Check that Ensuring a channel that doesn't exist succeeds
    call_async(q, conn.Requests, 'EnsureChannel', request_props (handle))

    ret, new_sig = q.expect_many(
        EventPattern('dbus-return', method='EnsureChannel'),
        EventPattern('dbus-signal', signal='NewChannels'),
        )

    assert len(ret.value) == 3
    yours, path, emitted_props = ret.value

    # The channel was created in response to the call, and we were the only
    # requestor, so we should get Yours=True
    assert yours, ret.value

    check_props(emitted_props, self_handle, handle, jid)

    assert len(new_sig.args) == 1
    assert len(new_sig.args[0]) == 1        # one channel
    assert len(new_sig.args[0][0]) == 2     # two struct members
    assert new_sig.args[0][0][0] == path
    assert new_sig.args[0][0][1] == emitted_props

    properties = conn.GetAll(
            'im.telepathy1.Connection.Interface.Requests',
            dbus_interface=dbus.PROPERTIES_IFACE)

    assert new_sig.args[0][0] in properties['Channels'], \
            (new_sig.args[0][0], properties['Channels'])


    # Now try Ensuring a channel which already exists
    call_async(q, conn.Requests, 'EnsureChannel', request_props (handle))
    ret_ = q.expect('dbus-return', method='EnsureChannel')

    assert len(ret_.value) == 3
    yours_, path_, emitted_props_ = ret_.value

    # Someone's already responsible for this channel, so we should get
    # Yours=False
    assert not yours_, ret_.value
    assert path == path_, (path, path_)
    assert emitted_props == emitted_props_, (emitted_props, emitted_props_)


def test_request_ensure(q, conn, self_handle, jid, handle):
    """
    Test Creating a non-existant channel, then Ensuring the same channel.
    The call to Ensure should succeed with Yours=False.
    """

    call_async(q, conn.Requests, 'CreateChannel', request_props (handle))

    ret, new_sig = q.expect_many(
        EventPattern('dbus-return', method='CreateChannel'),
        EventPattern('dbus-signal', signal='NewChannels'),
        )

    assert len(ret.value) == 2
    path, emitted_props = ret.value

    check_props(emitted_props, self_handle, handle, jid)

    assert len(new_sig.args) == 1
    assert len(new_sig.args[0]) == 1        # one channel
    assert len(new_sig.args[0][0]) == 2     # two struct members
    assert new_sig.args[0][0][0] == path
    assert new_sig.args[0][0][1] == emitted_props

    properties = conn.GetAll(
            'im.telepathy1.Connection.Interface.Requests',
            dbus_interface=dbus.PROPERTIES_IFACE)

    assert new_sig.args[0][0] in properties['Channels'], \
            (new_sig.args[0][0], properties['Channels'])


    # Now try Ensuring that same channel.
    call_async(q, conn.Requests, 'EnsureChannel', request_props (handle))
    ret_ = q.expect('dbus-return', method='EnsureChannel')

    assert len(ret_.value) == 3
    yours_, path_, emitted_props_ = ret_.value

    # Someone's already responsible for this channel, so we should get
    # Yours=False
    assert not yours_, ret_.value
    assert path == path_, (path, path_)
    assert emitted_props == emitted_props_, (emitted_props, emitted_props_)


def check_props(props, self_handle, handle, jid):
    assert props['im.telepathy1.Channel.ChannelType'] ==\
            'im.telepathy1.Channel.Type.Text'
    assert props['im.telepathy1.Channel.'
            'TargetHandleType'] == 1
    assert props['im.telepathy1.Channel.TargetHandle'] ==\
            handle
    assert props['im.telepathy1.Channel.TargetID'] == jid
    assert props['im.telepathy1.Channel.'
            'Requested'] == True
    assert props['im.telepathy1.Channel.'
            'InitiatorHandle'] == self_handle
    assert props['im.telepathy1.Channel.'
            'InitiatorID'] == 'test@localhost'


def request_props(handle):
    return { 'im.telepathy1.Channel.ChannelType':
                'im.telepathy1.Channel.Type.Text',
             'im.telepathy1.Channel.TargetHandleType': 1,
             'im.telepathy1.Channel.TargetHandle': handle,
           }


if __name__ == '__main__':
    exec_test(test)

