#ifndef TEST_NETDB_H
#define TEST_NETDB_H
struct hostent { char **h_addr_list; };
struct hostent *gethostbyname(const char *host);
#endif
