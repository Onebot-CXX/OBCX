/*
 * mcrcon.h - Minecraft RCON client library header
 *
 * Based on mcrcon by Tiiffi (https://github.com/Tiiffi/mcrcon)
 * Modified for use as a library in OBCX plugin system.
 */

#ifndef MCRCON_H
#define MCRCON_H

#ifdef __cplusplus
extern "C" {
#endif

/**
 * Connect to an RCON server.
 *
 * @param host Server hostname or IP address
 * @param port Server port as string (e.g., "25575")
 * @return Socket descriptor on success, -1 on failure
 */
int mcrcon_connect(const char *host, const char *port);

/**
 * Close an RCON connection.
 *
 * @param sd Socket descriptor from mcrcon_connect()
 */
void mcrcon_close(int sd);

/**
 * Authenticate with the RCON server.
 *
 * @param sock Socket descriptor from mcrcon_connect()
 * @param passwd RCON password
 * @return 1 on success, 0 on failure
 */
int mcrcon_auth(int sock, const char *passwd);

/**
 * Execute an RCON command and get the response.
 *
 * @param sock Socket descriptor from mcrcon_connect()
 * @param command Command to execute
 * @param response Buffer to store the response (can be NULL)
 * @param response_size Size of response buffer
 * @return 1 on success, 0 on failure
 */
int mcrcon_command(int sock, const char *command, char *response,
                   int response_size);

#ifdef __cplusplus
}
#endif

#endif /* MCRCON_H */
