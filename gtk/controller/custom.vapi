using GLib;

namespace Custom {

	[CCode (cname = "g_warn_if", cheader_filename = "custom.h")]
	public bool warn_if(bool condition);
}

namespace Spice {
	[CCode (cheader_filename = "namedpipe.h")]
	public class NamedPipeListener: SocketListener {
		[CCode (has_construct_function = false)]
		public NamedPipeListener ();
		public async unowned GLib.SocketConnection accept_async (GLib.Cancellable? cancellable = null, out GLib.Object? source_object = null) throws GLib.Error;
		public void add_named_pipe(NamedPipe namedpipe);
	}

	[CCode (cheader_filename = "namedpipe.h")]
	public class NamedPipeConnection: SocketConnection {
		[CCode (type = "GSocketConnection*", has_construct_function = false)]
		public NamedPipeConnection (GLib.IOStream base_io_stream, GLib.Socket socket);
	}

	[CCode (cheader_filename = "namedpipe.h")]
	public class NamedPipe: Object {
		public NamedPipe (string name);
	}
}
