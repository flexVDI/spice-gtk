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

public class SpiceMenuItem: Object {

	public SpiceMenu submenu;
	public int parent_id;
	public int id;
	public string text;
	public Controller.MenuFlags flags;

	public SpiceMenuItem.from_string (string str) throws ControllerError {
		var params = str.split (Controller.MENU_PARAM_DELIMITER);
		if (warn_if (params.length != 5))
			throw new ControllerError.VALUE(""); /* Vala: why is it mandatory to give a string? */
		parent_id = int.parse (params[0]);
		id = int.parse (params[1]);
		text = params[2];
		flags = (Controller.MenuFlags)int.parse (params[3]);

		submenu = new SpiceMenu ();
	}

	public string to_string () {
		var sub = submenu.to_string ();
		var str = @"pid: $parent_id, id: $id, text: \"$text\", flags: $flags";
		foreach (var l in sub.to_string ().split ("\n")) {
			if (l == "")
				continue;
			str += @"\n    $l";
		}
		return str;
	}
}

public class SpiceMenu: Object {

	public List<SpiceMenuItem> items;

	public SpiceMenu? find_id (int id) {
		if (id == 0)
			return this;

		foreach (var item in items) {
			if (item.id == id)
				return item.submenu;

			var menu = item.submenu.find_id (id);
			if (menu != null)
				return menu;
		}

		return null;
	}

	public SpiceMenu.from_string (string str) {
		foreach (var itemstr  in str.split (Controller.MENU_ITEM_DELIMITER)) {
			try {
				if (itemstr.length == 0)
					continue;
				var item = new SpiceMenuItem.from_string (itemstr);
				var parent = find_id (item.parent_id);
				if (parent == null)
					throw new ControllerError.VALUE("Invalid parent menu id");
				parent.items.append (item);
			} catch (ControllerError e) {
				warning (e.message);
			}
		}
	}

	public string to_string () {
		var str = "";
		foreach (var i in items)
			str += @"\n$i";
		return str;
	}
}
