/* -*- Mode: C; c-basic-offset: 4; indent-tabs-mode: nil -*- */
/*
   Copyright (C) 2011 Red Hat, Inc.

   Red Hat Authors:
   Hans de Goede <hdegoede@redhat.com>

   This library is free software; you can redistribute it and/or
   modify it under the terms of the GNU Lesser General Public
   License as published by the Free Software Foundation; either
   version 2.1 of the License, or (at your option) any later version.

   This library is distributed in the hope that it will be useful,
   but WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
   Lesser General Public License for more details.

   You should have received a copy of the GNU Lesser General Public
   License along with this library; if not, see <http://www.gnu.org/licenses/>.
*/
#ifndef __SPICE_CLIENT_USBREDIR_CHANNEL_PRIV_H__
#define __SPICE_CLIENT_USBREDIR_CHANNEL_PRIV_H__

#include <gusb/gusb-context.h>
#include <gusb/gusb-device.h>

#include "spice-client.h"

G_BEGIN_DECLS

void spice_usbredir_channel_connect_async(SpiceUsbredirChannel *channel,
                                          GUsbContext          *context,
                                          GUsbDevice           *device,
                                          GCancellable         *cancellable,
                                          GAsyncReadyCallback   callback,
                                          gpointer              user_data);
gboolean spice_usbredir_channel_connect_finish(SpiceUsbredirChannel *channel,
                                               GAsyncResult         *res,
                                               GError              **err);

void spice_usbredir_channel_disconnect(SpiceUsbredirChannel *channel);

GUsbDevice *spice_usbredir_channel_get_device(SpiceUsbredirChannel *channel);

void spice_usbredir_channel_do_write(SpiceUsbredirChannel *channel);

G_END_DECLS

#endif /* __SPICE_CLIENT_USBREDIR_CHANNEL_PRIV_H__ */
