/* -*- Mode: C; c-basic-offset: 4; indent-tabs-mode: nil -*- */
/*
   Copyright (C) 2010 Red Hat, Inc.

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
#ifndef _TCP_H_
# define _TCP_H_

#include <netinet/in.h>
#include <arpa/inet.h>
#include <netdb.h>

G_BEGIN_DECLS

extern int tcp_verbose;

int tcp_connect(struct addrinfo *ai,
                char *addr, char *port,
                char *host, char *serv);

G_END_DECLS

#endif // _TCP_H_
