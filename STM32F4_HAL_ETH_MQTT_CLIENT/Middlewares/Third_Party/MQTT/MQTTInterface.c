#include "MQTTInterface.h"
#include "stm32f4xx_hal.h"

#include <string.h>
#include "lwip.h"
#include "lwip/api.h"
#include "lwip/sockets.h"

#define MQTT_PORT	1883
#define SERVER_IP1	192
#define SERVER_IP2	168
#define SERVER_IP3	1
#define SERVER_IP4	227

uint32_t MilliTimer;

//Timer functions
char TimerIsExpired(Timer *timer) {
	long left = timer->end_time - MilliTimer;
	return (left < 0);
}

void TimerCountdownMS(Timer *timer, unsigned int timeout) {
	timer->end_time = MilliTimer + timeout;
}

void TimerCountdown(Timer *timer, unsigned int timeout) {
	timer->end_time = MilliTimer + (timeout * 1000);
}

int TimerLeftMS(Timer *timer) {
	long left = timer->end_time - MilliTimer;
	return (left < 0) ? 0 : left;
}

void TimerInit(Timer *timer) {
	timer->end_time = 0;
}

#ifdef MQTT_LWIP_SOCKET
void NewNetwork(Network *n) {
	n->socket = 0; //clear
	n->mqttread = net_read; //receive function
	n->mqttwrite = net_write; //send function
	n->disconnect = net_disconnect; //disconnection function
}

int ConnectNetwork(Network *n, char *ip, int port) {
	struct sockaddr_in server_addr;

	if(n->socket)
	{
		close(n->socket);
	}

	n->socket = socket(PF_INET, SOCK_STREAM, 0); //create socket
	if(n->socket < 0)
	{
		n->socket = 0;
		return -1;
	}

	memset(&server_addr, 0, sizeof(struct sockaddr_in)); //broker address info
	server_addr.sin_family = AF_INET;
	server_addr.sin_addr.s_addr = inet_addr(ip);
	server_addr.sin_port = htons(port);

	if(connect(n->socket, (struct sockaddr*)&server_addr, sizeof(struct sockaddr_in)) < 0) //connect to the broker
	{
		close(n->socket);
		return -1;
	}
	return 0;
}

int net_read(Network *n, unsigned char *buffer, int len, int timeout_ms) {
	int available;

	/* !!! LWIP_SO_RCVBUF must be enabled !!! */
	if(ioctl(n->socket, FIONREAD, &available) < 0) return -1; //check receive buffer

	if(available > 0)
	{
		return recv(n->socket, buffer, len, 0);
	}

	return 0;
}

int net_write(Network *n, unsigned char *buffer, int len, int timeout_ms) {
	return send(n->socket, buffer, len, 0);
}

void net_disconnect(Network *n) {
	close(n->socket);
	n->socket = 0;
}

#elif MQTT_LWIP_NETCONN
void NewNetwork(Network *n) {
	n->conn = NULL;
	n->buf = NULL;
	n->offset = 0;

	n->mqttread = net_read;
	n->mqttwrite = net_write;
	n->disconnect = net_disconnect;
}

int ConnectNetwork(Network *n, char *ip, int port) {
	err_t err;
	ip_addr_t server_ip;

	ipaddr_aton(ip, &server_ip);

	n->conn = netconn_new(NETCONN_TCP);
	if (n->conn != NULL) {
		err = netconn_connect(n->conn, &server_ip, port);

		if (err != ERR_OK) {
			netconn_delete(n->conn); //free memory
			return -1;
		}
	}

	return 0;
}

int net_read(Network *n, unsigned char *buffer, int len, int timeout_ms) {
	int rc;
	struct netbuf *inbuf;
	int offset = 0;
	int bytes = 0;

	while(bytes < len) {
		if(n->buf != NULL) {
			inbuf = n->buf;
			offset = n->offset;
			rc = ERR_OK;
		} else {
			rc = netconn_recv(n->conn, &inbuf);
			offset = 0;
		}

		if(rc != ERR_OK) {
			if(rc != ERR_TIMEOUT) {
				bytes = -1;
			}
			break;
		} else {
			int nblen = netbuf_len(inbuf) - offset;
			if((bytes+nblen) > len) {
				netbuf_copy_partial(inbuf, buffer+bytes, len-bytes,offset);
				n->buf = inbuf;
				n->offset = offset + len - bytes;
				bytes = len;
			} else {
				netbuf_copy_partial(inbuf, buffer+bytes, nblen, offset);
				bytes += nblen;
				netbuf_delete(inbuf);
				n->buf = NULL;
				n->offset = 0;
			}
		}
	}
	return bytes;
}

int net_write(Network *n, unsigned char *buffer, int len, int timeout_ms) {
	int rc = netconn_write(n->conn, buffer, len, NETCONN_NOCOPY);
	if(rc != ERR_OK) return -1;
	return len;
}

void net_disconnect(Network *n) {
	netconn_close(n->conn); //close session
	netconn_delete(n->conn); //free memory
	n->conn = NULL;
}
#endif
