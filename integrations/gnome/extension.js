/* SPDX-License-Identifier: GPL-2.0-or-later */
const { Gio, GLib, Meta } = imports.gi;

const SERVICE = 'io.github.WangZhiChong2003.SnipLingo.Pointer';
const PATH = '/io/github/WangZhiChong2003/SnipLingo/Pointer';
const INTERFACE = `<node>
    <interface name="${SERVICE}">
        <method name="GetPointer">
            <arg name="x" type="i" direction="out"/>
            <arg name="y" type="i" direction="out"/>
        </method>
    </interface>
</node>`;

class SnipLingoPointer {
    enable() {
        if (!Meta.is_wayland_compositor())
            return;
        this._object = Gio.DBusExportedObject.wrapJSObject(INTERFACE, this);
        this._object.export(Gio.DBus.session, PATH);
        this._ownerId = Gio.bus_own_name_on_connection(
            Gio.DBus.session, SERVICE, Gio.BusNameOwnerFlags.NONE, null, null);
    }

    disable() {
        if (!this._object)
            return;
        Gio.bus_unown_name(this._ownerId);
        this._object.unexport();
        this._object = null;
    }

    GetPointerAsync(_params, invocation) {
        const connection = invocation.get_connection();
        // Limit requests to SnipLingo using the session bus's sender credentials.
        connection.call('org.freedesktop.DBus', '/org/freedesktop/DBus',
            'org.freedesktop.DBus', 'GetConnectionUnixProcessID',
            new GLib.Variant('(s)', [invocation.get_sender()]), new GLib.VariantType('(u)'),
            Gio.DBusCallFlags.NONE, 100, null, (_connection, result) => {
                let executable;
                try {
                    const [pid] = connection.call_finish(result).deep_unpack();
                    executable = GLib.path_get_basename(GLib.file_read_link(`/proc/${pid}/exe`));
                } catch (error) {
                    invocation.return_dbus_error('org.freedesktop.DBus.Error.AccessDenied', error.message);
                    return;
                }
                if (!this._object || executable !== 'sniplyngo') {
                    invocation.return_dbus_error('org.freedesktop.DBus.Error.AccessDenied',
                        'This service accepts requests from SnipLingo.');
                    return;
                }

                // In GNOME 42–44, XWayland and the compositor use the same logical screen space.
                const [x, y] = global.get_pointer();
                invocation.return_value(new GLib.Variant('(ii)', [x, y]));
            });
    }
}

function init() {
    return new SnipLingoPointer();
}
