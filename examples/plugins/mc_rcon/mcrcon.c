/*
 * Copyright (c) 2012-2021, Tiiffi <tiiffi at gmail>
 *
 * This software is provided 'as-is', without any express or implied
 * warranty. In no event will the authors be held liable for any damages
 * arising from the use of this software.
 *
 * Permission is granted to anyone to use this software for any purpose,
 * including commercial applications, and to alter it and redistribute it
 * freely, subject to the following restrictions:
 *
 *   1. The origin of this software must not be misrepresented; you must not
 *   claim that you wrote the original software. If you use this software
 *   in a product, an acknowledgment in the product documentation would be
 *   appreciated but is not required.
 *
 *   2. Altered source versions must be plainly marked as such, and must not be
 *   misrepresented as being the original software.
 *
 *   3. This notice may not be removed or altered from any source
 *   distribution.
 *
 * MODIFIED: Adapted for use as a library in OBCX plugin system.
 *           Removed main() and CLI-related code.
 *           Added rcon_command_get_response() for getting command output.
 */

#include "mcrcon.h"

#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

#ifdef _WIN32
#include <windows.h>
#include <winsock2.h>
#include <ws2tcpip.h>
#else
#include <arpa/inet.h>
#include <netdb.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <sys/types.h>
#endif

#define RCON_EXEC_COMMAND 2
#define RCON_AUTHENTICATE 3
#define RCON_RESPONSEVALUE 0
#define RCON_AUTH_RESPONSE 2
#define RCON_PID 0xBADC0DE

#define DATA_BUFFSIZE 4096

// rcon packet structure
typedef struct _rc_packet {
  int size;
  int id;
  int cmd;
  char data[DATA_BUFFSIZE];
} rc_packet;

// Forward declarations
static rc_packet *packet_build(int id, int cmd, const char *s1);
static int net_send_packet(int sd, rc_packet *packet);
static rc_packet *net_recv_packet(int sd);
static int net_clean_incoming(int sd, int size);

#ifdef _WIN32
static void net_init_WSA(void) {
  WSADATA wsadata;
  WORD version = MAKEWORD(2, 2);
  int err = WSAStartup(version, &wsadata);
  if (err != 0) {
    fprintf(stderr, "WSAStartup failed. Error: %d.\n", err);
  }
}
#endif

void mcrcon_close(int sd) {
  if (sd < 0)
    return;
#ifdef _WIN32
  closesocket(sd);
  WSACleanup();
#else
  close(sd);
#endif
}

int mcrcon_connect(const char *host, const char *port) {
  int sd;
  struct addrinfo hints;
  struct addrinfo *server_info, *p;

  memset(&hints, 0, sizeof hints);
  hints.ai_family = AF_UNSPEC;
  hints.ai_socktype = SOCK_STREAM;
  hints.ai_protocol = IPPROTO_TCP;

#ifdef _WIN32
  net_init_WSA();
#endif

  int ret = getaddrinfo(host, port, &hints, &server_info);
  if (ret != 0) {
    fprintf(stderr, "mcrcon: Name resolution failed for %s:%s\n", host, port);
    return -1;
  }

  for (p = server_info; p != NULL; p = p->ai_next) {
    sd = socket(p->ai_family, p->ai_socktype, p->ai_protocol);
    if (sd == -1)
      continue;

    ret = connect(sd, p->ai_addr, p->ai_addrlen);
    if (ret == -1) {
      mcrcon_close(sd);
      continue;
    }
    break;
  }

  if (p == NULL) {
    fprintf(stderr, "mcrcon: Connection failed to %s:%s\n", host, port);
    freeaddrinfo(server_info);
    return -1;
  }

  freeaddrinfo(server_info);
  return sd;
}

static int net_send_packet(int sd, rc_packet *packet) {
  int len;
  int total = 0;
  int bytesleft;
  int ret = -1;

  bytesleft = len = packet->size + sizeof(int);

  while (total < len) {
    ret = send(sd, (char *)packet + total, bytesleft, 0);
    if (ret == -1)
      break;
    total += ret;
    bytesleft -= ret;
  }

  return ret == -1 ? -1 : 1;
}

static rc_packet *net_recv_packet(int sd) {
  int psize;
  static rc_packet packet = {0, 0, 0, {0x00}};

  int ret = recv(sd, (char *)&psize, sizeof(int), 0);

  if (ret == 0) {
    fprintf(stderr, "mcrcon: Connection lost.\n");
    return NULL;
  }

  if (ret != sizeof(int)) {
    fprintf(stderr, "mcrcon: recv() failed. Invalid packet size (%d).\n", ret);
    return NULL;
  }

  if (psize < 10 || psize > DATA_BUFFSIZE) {
    fprintf(stderr, "mcrcon: Invalid packet size (%d).\n", psize);
    if (psize > DATA_BUFFSIZE || psize < 0)
      psize = DATA_BUFFSIZE;
    net_clean_incoming(sd, psize);
    return NULL;
  }

  packet.size = psize;

  int received = 0;
  while (received < psize) {
    ret =
        recv(sd, (char *)&packet + sizeof(int) + received, psize - received, 0);
    if (ret == 0) {
      fprintf(stderr, "mcrcon: Connection lost.\n");
      return NULL;
    }
    received += ret;
  }

  return &packet;
}

static int net_clean_incoming(int sd, int size) {
  char tmp[DATA_BUFFSIZE];
  if (size > DATA_BUFFSIZE)
    size = DATA_BUFFSIZE;
  return recv(sd, tmp, size, 0);
}

static rc_packet *packet_build(int id, int cmd, const char *s1) {
  static rc_packet packet = {0, 0, 0, {0x00}};

  int len = strlen(s1);
  if (len >= DATA_BUFFSIZE) {
    fprintf(stderr, "mcrcon: Command string too long (%d). Max: %d.\n", len,
            DATA_BUFFSIZE - 1);
    return NULL;
  }

  packet.size = sizeof(int) * 2 + len + 2;
  packet.id = id;
  packet.cmd = cmd;
  strncpy(packet.data, s1, DATA_BUFFSIZE - 1);
  packet.data[DATA_BUFFSIZE - 1] = '\0';

  return &packet;
}

int mcrcon_auth(int sock, const char *passwd) {
  rc_packet *packet = packet_build(RCON_PID, RCON_AUTHENTICATE, passwd);
  if (packet == NULL)
    return 0;

  int ret = net_send_packet(sock, packet);
  if (!ret)
    return 0;

  packet = net_recv_packet(sock);
  if (packet == NULL)
    return 0;

  return packet->id == -1 ? 0 : 1;
}

int mcrcon_command(int sock, const char *command, char *response,
                   int response_size) {
  if (response && response_size > 0) {
    response[0] = '\0';
  }

  rc_packet *packet = packet_build(RCON_PID, RCON_EXEC_COMMAND, command);
  if (packet == NULL) {
    return 0;
  }

  if (net_send_packet(sock, packet) == -1) {
    return 0;
  }

  packet = net_recv_packet(sock);
  if (packet == NULL)
    return 0;

  if (packet->id != RCON_PID)
    return 0;

  // Copy response data (strip Minecraft color codes)
  if (response && response_size > 0) {
    int j = 0;
    for (int i = 0; packet->data[i] != 0 && j < response_size - 1; ++i) {
      // Skip Minecraft color codes (0xc2 0xa7 followed by color char)
      if ((unsigned char)packet->data[i] == 0xc2 &&
          (unsigned char)packet->data[i + 1] == 0xa7) {
        i += 2;
        continue;
      }
      response[j++] = packet->data[i];
    }
    response[j] = '\0';
  }

  return 1;
}
