// Copyright (C) 2011 Red Hat, Inc.

// This library is free software; you can redistribute it and/or
// modify it under the terms of the GNU Lesser General Public
// License as published by the Free Software Foundation; either
// version 2.1 of the License, or (at your option) any later version.

// This library is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
// Lesser General Public License for more details.

// You should have received a copy of the GNU Lesser General Public
// License along with this library; if not, see <http://www.gnu.org/licenses/>.

using GLib;
using Custom;
using SpiceProtocol;

public errordomain ControllerError {
	VALUE,
}

public class SpiceController: Object {
	public string host { private set; get; }
	public uint32 port { private set; get; }
	public uint32 sport { private set; get; }
	public string password { private set; get; }
	public Controller.Display display_flags { private set; get; }
	public string tls_ciphers { private set; get; }
	public string host_subject { private set; get; }
	public string ca_file { private set; get; }
	public string title { private set; get; }
	public string hotkeys { private set; get; }
	public string[] secure_channels { private set; get; }
	public string[] disable_channels { private set; get; }
	public SpiceMenu? menu  { private set; get; }

	public signal void do_connect ();
	public signal void show ();
	public signal void hide ();

	public void menu_item_click_msg (int32 item_id) {
		var msg = Controller.MsgValue ();
		msg.base.size = (uint32)sizeof (Controller.MsgValue);
		msg.base.id = Controller.MsgId.MENU_ITEM_CLICK;
		msg.value = item_id;
		unowned uint8[] p = ((uint8[])(&msg))[0:msg.base.size];
		send_msg (p);
	}

	public async bool send_msg (uint8[] p) throws GLib.Error {
		// vala FIXME: pass Controller.Msg instead
		// vala doesn't keep reference on the struct in async methods
		// it copies only base, which is not enough to transmit the whole
		// message.
		try {
			if (excl_connection != null) {
				yield excl_connection.output_stream.write_async (p);
			} else {
				foreach (var c in clients)
					yield c.output_stream.write_async (p);
			}
		} catch (GLib.Error e) {
			warning (e.message);
		}

		return true;
	}

	private SocketConnection? excl_connection;
	private int nclients;
	List<SocketConnection> clients;

	private bool handle_message (Controller.Msg msg) {
		var v = (Controller.MsgValue*)(&msg);
		var d = (Controller.MsgData*)(&msg);
		unowned string str = (string)(&d.data);

		switch (msg.id) {
		case Controller.MsgId.HOST:
			host = str;
			break;
		case Controller.MsgId.PORT:
			port = v.value;
			break;
		case Controller.MsgId.SPORT:
			sport = v.value;
			break;
		case Controller.MsgId.PASSWORD:
			password = str;
			break;

		case Controller.MsgId.SECURE_CHANNELS:
			secure_channels = str.split(",");
			break;

		case Controller.MsgId.DISABLE_CHANNELS:
			disable_channels = str.split(",");
			break;

		case Controller.MsgId.TLS_CIPHERS:
			tls_ciphers = str;
			break;
		case Controller.MsgId.CA_FILE:
			ca_file = str;
			break;
		case Controller.MsgId.HOST_SUBJECT:
			host_subject = str;
			break;

		case Controller.MsgId.FULL_SCREEN:
			display_flags = (Controller.Display)v.value;
			break;
		case Controller.MsgId.SET_TITLE:
			title = str;
			break;

		case Controller.MsgId.CREATE_MENU:
			menu = new SpiceMenu.from_string (str);
			break;
		case Controller.MsgId.DELETE_MENU:
			menu = null;
			break;

		// ignore SEND_CAD
		case Controller.MsgId.HOTKEYS:
			hotkeys = str;
			break;

		case Controller.MsgId.CONNECT:
			do_connect ();
			break;
		case Controller.MsgId.SHOW:
			show ();
			break;
		case Controller.MsgId.HIDE:
			hide ();
			break;
		default:
			warn_if_reached ();
			return false;
		}
		return true;
	}

	private async void handle_client (SocketConnection c) throws GLib.Error {
		var init = Controller.Init ();
		var excl = false;
		unowned uint8[] p = null;

		debug ("new socket client, reading init header");

		p = ((uint8[])(&init))[0:sizeof(Controller.InitHeader)]; // FIXME vala
		var read = yield c.input_stream.read_async (p);
		if (warn_if (read != sizeof (Controller.InitHeader)))
			return;
		if (warn_if (init.base.magic != Controller.MAGIC))
			return;
		if (warn_if (init.base.version != Controller.VERSION))
			return;
		if (warn_if (init.base.size < sizeof (Controller.Init)))
			return;

		p = ((uint8[])(&init.credentials))[0:init.base.size - sizeof(Controller.InitHeader)];
		read = yield c.input_stream.read_async (p);
		if (warn_if (read != (init.base.size - sizeof (Controller.InitHeader))))
			return;
		if (warn_if (init.credentials != 0))
			return;
		if (warn_if (excl_connection != null))
			return;

		excl = (bool)(init.flags & Controller.Flag.EXCLUSIVE);
		if (excl) {
			if (nclients > 1) {
				warning (@"Can't make the client exclusive, there is already $nclients connected clients");
				return;
			}
			excl_connection = c;
		}

		var t = new uint8[sizeof(Controller.Msg)];
		for (;;) {
			read = yield c.input_stream.read_async (t[0:sizeof(Controller.Msg)]);
			if (read == 0)
				break;

			if (warn_if (read != sizeof (Controller.Msg)))
				break;

			var msg = (Controller.Msg*)t;
			if (warn_if (msg.size < sizeof (Controller.Msg)))
				break;

			if (msg.size > sizeof (Controller.Msg)) {
				t.resize ((int)msg.size);
				msg = (Controller.Msg*)t;
				read = yield c.input_stream.read_async (t[sizeof(Controller.Msg):msg.size]);
				if (read == 0)
					break;
				if (warn_if (read != msg.size - sizeof(Controller.Msg)))
					break;
			}

			handle_message (*msg);
		}

		if (excl)
			excl_connection = null;
	}

	public SpiceController() {
	}

	public async void listen (string? addr = null) throws GLib.Error, ControllerError
	{
		if (addr == null)
			addr = (string*)"%s".printf (Environment.get_variable ("SPICE_XPI_SOCKET")); // FIXME vala...
		if (addr == null)
			throw new ControllerError.VALUE ("Missing SPICE_XPI_SOCKET");
		FileUtils.unlink (addr);

		var listener = new SocketListener ();
		listener.add_address (new UnixSocketAddress.with_type (addr, addr.length, UnixSocketAddressType.PATH),
							  SocketType.STREAM, SocketProtocol.DEFAULT);

		for (;;) {
			var c = yield listener.accept_async ();
			nclients += 1;
			clients.append (c);
			try {
				yield handle_client (c);
			} catch (GLib.Error e) {
				warning (e.message);
			}
			c.close ();
			clients.remove (c);
			nclients -= 1;
		}
	}
}
