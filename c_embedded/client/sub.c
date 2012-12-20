/*
 * This file is part of libemqtt.
 *
 * libemqtt is free software: you can redistribute it and/or modify
 * it under the terms of the GNU Lesser General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * libemqtt is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.

 * You should have received a copy of the GNU General Public License
 * along with libemqtt.  If not, see <http://www.gnu.org/licenses/>.
 */

/*
 *
 * Created by Vicente Ruiz Rodríguez
 * Copyright 2012 Vicente Ruiz Rodríguez <vruiz2.0@gmail.com>. All rights reserved.
 *
 */

#include <libemqtt.h>

#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <linux/tcp.h>
#include <signal.h>


#define RCVBUFSIZE 1024
uint8_t packet_buffer[RCVBUFSIZE];

int keepalive = 5;
mqtt_broker_handle_t broker;

int socket_id;



int send_packet(void* socket_info, const void* buf, unsigned int count)
{
	int fd = *((int*)socket_info);
	return send(fd, buf, count, 0);
}

int init_socket(mqtt_broker_handle_t* broker, const char* hostname, short port, int keepalive)
{
	int flag = 1;

	// Create the socket
	if((socket_id = socket(PF_INET, SOCK_STREAM, 0)) < 0)
		return -1;

	// Disable Nagle Algorithm
	if (setsockopt(socket_id, IPPROTO_TCP, TCP_NODELAY, (char*)&flag, sizeof(flag)) < 0)
		return -2;

	struct sockaddr_in socket_address;
	// Create the stuff we need to connect
	socket_address.sin_family = AF_INET;
	socket_address.sin_port = htons(port);
	socket_address.sin_addr.s_addr = inet_addr(hostname);

	// Connect the socket
	if((connect(socket_id, (struct sockaddr*)&socket_address, sizeof(socket_address))) < 0)
		return -1;

	// MQTT stuffs
	mqtt_set_alive(broker, keepalive);
	broker->socket_info = (void*)&socket_id;
	broker->send = send_packet;

	return 0;
}

int close_socket(mqtt_broker_handle_t* broker)
{
	int fd = *((int*)broker->socket_info);
	return close(fd);
}




int read_packet(int timeout)
{
	if(timeout > 0)
	{
		fd_set readfds;
		struct timeval tmv;

		// Initialize the file descriptor set
		FD_ZERO (&readfds);
		FD_SET (socket_id, &readfds);

		// Initialize the timeout data structure
		tmv.tv_sec = timeout;
		tmv.tv_usec = 0;

		// select returns 0 if timeout, 1 if input available, -1 if error
		if(select(1, &readfds, NULL, NULL, &tmv))
			return -2;
	}

	int total_bytes = 0, bytes_rcvd, packet_length;
	memset(packet_buffer, 0, sizeof(packet_buffer));

	while(total_bytes < 2) // Reading fixed header
	{
		if((bytes_rcvd = recv(socket_id, (packet_buffer+total_bytes), RCVBUFSIZE, 0)) <= 0)
			return -1;
		total_bytes += bytes_rcvd; // Keep tally of total bytes
	}

	packet_length = packet_buffer[1] + 2; // Remaining length + fixed header length

	while(total_bytes < packet_length) // Reading the packet
	{
		if((bytes_rcvd = recv(socket_id, (packet_buffer+total_bytes), RCVBUFSIZE, 0)) <= 0)
			return -1;
		total_bytes += bytes_rcvd; // Keep tally of total bytes
	}

	return packet_length;
}


void alive(int sig)
{
	printf("Timeout! Sending ping...\n");
	mqtt_ping(&broker);

	alarm(keepalive);
}

void term(int sig)
{
	printf("Goodbye!\n");
	// >>>>> DISCONNECT
	mqtt_disconnect(&broker);
	close_socket(&broker);

	exit(0);
}




int main()
{
	int packet_length;
	uint16_t msg_id, msg_id_rcv;

	mqtt_init(&broker, "sancho");
	mqtt_init_auth(&broker, "quijote", "rocinante");
	init_socket(&broker, "192.168.10.40", 1883, keepalive);

	// >>>>> CONNECT
	mqtt_connect(&broker);
	// <<<<< CONNACK
	packet_length = read_packet(1);
	if(packet_length < 0)
	{
		fprintf(stderr, "Error(%d) on read packet!\n", packet_length);
		return -1;
	}

	if(MQTTParseMessageType(packet_buffer) != MQTT_MSG_CONNACK)
	{
		fprintf(stderr, "CONNACK expected!\n");
		return -2;
	}

	if(packet_buffer[3] != 0x00)
	{
		fprintf(stderr, "CONNACK failed!\n");
		return -2;
	}

	// Signals after connect MQTT
	signal(SIGALRM, alive);
	alarm(keepalive);
	signal(SIGINT, term);

	// >>>>> SUBSCRIBE
	mqtt_subscribe(&broker, "hello/emqtt", &msg_id);
	// <<<<< SUBACK
	packet_length = read_packet(1);
	if(packet_length < 0)
	{
		fprintf(stderr, "Error(%d) on read packet!\n", packet_length);
		return -1;
	}

	if(MQTTParseMessageType(packet_buffer) != MQTT_MSG_SUBACK)
	{
		fprintf(stderr, "SUBACK expected!\n");
		return -2;
	}

	MQTTParseMessageId(packet_buffer, msg_id_rcv);
	if(msg_id != msg_id_rcv)
	{
		fprintf(stderr, "%d message id was expected, but %d message id was found!\n", msg_id, msg_id_rcv);
		return -3;
	}

	while(1)
	{
		// <<<<<
		packet_length = read_packet(0);
		if(packet_length == -1)
		{
			fprintf(stderr, "Error(%d) on read packet!\n", packet_length);
			return -1;
		}
		else if(packet_length > 0)
		{
			printf("Packet Header: 0x%x...\n", packet_buffer[0]);
			if(MQTTParseMessageType(packet_buffer) == MQTT_MSG_PUBLISH)
			{
				char topic[255], msg[255];
				int len;
				MQTTParsePublishTopic(packet_buffer, topic, len);
				topic[len] = '\0'; // for printf
				MQTTParsePublishMessage(packet_buffer, msg, len);
				msg[len] = '\0'; // for printf
				printf("%s %s\n", topic, msg);
			}
		}

	}

	return 0;
}
