namespace Custom {

	[CCode (cname = "g_warn_if", cheader_filename = "custom.h")]
	public bool warn_if(bool condition);
}
