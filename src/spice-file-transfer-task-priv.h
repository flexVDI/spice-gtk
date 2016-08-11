/*
   Copyright (C) 2016 Red Hat, Inc.

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

#ifndef __SPICE_FILE_TRANSFER_TASK_PRIV_H__
#define __SPICE_FILE_TRANSFER_TASK_PRIV_H__

#include "config.h"

#include <spice/vd_agent.h>

#include "spice-client.h"
#include "channel-main.h"
#include "spice-file-transfer-task.h"
#include "spice-channel-priv.h"

G_BEGIN_DECLS

void spice_file_transfer_task_completed(SpiceFileTransferTask *self, GError *error);
guint32 spice_file_transfer_task_get_id(SpiceFileTransferTask *self);
SpiceMainChannel *spice_file_transfer_task_get_channel(SpiceFileTransferTask *self);
GCancellable *spice_file_transfer_task_get_cancellable(SpiceFileTransferTask *self);
GHashTable *spice_file_transfer_task_create_tasks(GFile **files,
                                                  SpiceMainChannel *channel,
                                                  GFileCopyFlags flags,
                                                  GCancellable *cancellable);
void spice_file_transfer_task_init_task_async(SpiceFileTransferTask *self,
                                              GAsyncReadyCallback callback,
                                              gpointer userdata);
GFileInfo *spice_file_transfer_task_init_task_finish(SpiceFileTransferTask *xfer_task,
                                                     GAsyncResult *result,
                                                     GError **error);
void spice_file_transfer_task_read_async(SpiceFileTransferTask *self,
                                         GAsyncReadyCallback callback,
                                         gpointer userdata);
gssize spice_file_transfer_task_read_finish(SpiceFileTransferTask *self,
                                            GAsyncResult *result,
                                            char **buffer,
                                            GError **error);
gboolean spice_file_transfer_task_is_completed(SpiceFileTransferTask *self);

G_END_DECLS

#endif /* __SPICE_FILE_TRANSFER_TASK_PRIV_H__ */
