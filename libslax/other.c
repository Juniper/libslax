
static void
dbg_set_remote_debug (int fd)
{
    remote_fp = fdopen(fd, "r+");
    if (remote_fp == NULL) {
	dbg_output("Remote debugging file pointer open failed, " 
		   "remote debugging disabled");
    }
}

/*
 * Listen on first available port between 5000 to 5009
 */
void
dbg_remote_open (void)
{
    int fd, remote_fd, tmp, i;
    int port;
    struct sockaddr_in sockaddr;

    fd = socket(PF_INET, SOCK_STREAM, 0);

    /*
     * Reuse of port
     */
    setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, (char *) &tmp, sizeof (tmp));

    /*
     * Bind to one of the available port starting from 5000
     * Try maximum 10 times
     */
    port = 5000;
    for (i = 0; i < REMOTE_PORT_MAX_TRY; i++) {

    	sockaddr.sin_family = PF_INET;
	sockaddr.sin_port = htons(port);
	sockaddr.sin_addr.s_addr = INADDR_ANY;

	if (bind(fd, (struct sockaddr *) &sockaddr, sizeof (sockaddr)) == -1) {

	    if (errno == EADDRINUSE) {
		dbg_output("Can't bind to port [%d], trying next ...", 
			   port);
		port++;
		continue; 
	    }

	    dbg_output("Can't bind address, remote debugging disabled");
	    return;
	} else {
	    /* Bind successful */
	    break;
	}
    }

    if (i == REMOTE_PORT_MAX_TRY) {
	dbg_output("Can't bind to any port, all %d tries failed "
		   ", remote debugging disabled", REMOTE_PORT_MAX_TRY);
	return;
    }

    if (listen(fd, 1) == -1) {
	dbg_output("Listen failed, remote debugging disabled");
	return;
    }

    dbg_output("Listening on port %d for remote debugging", port);

    tmp = sizeof(sockaddr);
    remote_fd = accept(fd, (struct sockaddr *) &sockaddr, &tmp);
    if (remote_fd == -1) {
	dbg_output("Accept failed, remote debugging disabled");
	return;
    }

     /* 
      * Enable TCP keep alive process. 
      */
    tmp = 1;
    setsockopt(fd, SOL_SOCKET, SO_KEEPALIVE, (char *) &tmp, sizeof (tmp));

    close(fd);

    dbg_output("Remote debugging from host %s\n", inet_ntoa(sockaddr.sin_addr));

    dbg_set_remote_debug(remote_fd);

}

