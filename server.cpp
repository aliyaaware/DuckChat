#include <sys/types.h>
#include <uuid/uuid.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <netdb.h>
#include <unistd.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <ctype.h>
#include <fcntl.h>
#include <string>
#include <map>
#include <iostream>
#include <time.h>
#include <list>
#include <signal.h>
#include <time.h>
//get random number
#include <cstring>
#include <fstream>
#include <sstream>
//#include "hash.h"
#include "duckchat.h"

using namespace std;


#define MAX_CONNECTIONS 10
#define HOSTNAME_MAX 100
#define MAX_MESSAGE_LEN 65536

//typedef map<string,string> channel_type; //<username, ip+port in string>
typedef map<string,struct sockaddr_in> channel_type; //<username, sockaddr_in of user>
struct timeval timeout;
string this_server_name;
int sock_for_listenig; //socket for listening
struct sockaddr_in server;
int last_refresh_time = time(0);

//map from username to User's sockaddr_in
// holds users and their sockaddr_in
map<string,struct sockaddr_in> usernames; //<username, sockaddr_in of user>

 // holds users that have logged in and their activity status
map<string,int> active_usernames; //0-inactive , 1-active

// holds users that have logged in and their ip+port in string form
map<string,string> rev_usernames; //<ip+port in string, username>

// holds channels, the users who have joined that channel 
map<string,channel_type> channels;

//map of chanels that point to all of the users on the channel
map<string, map<string, struct sockaddr_in> > server_neighbors;
map<string,struct sockaddr_in> servers;//<ip+port in string, sockaddr_in of server>
map<string,int> channel_subscriptions; //<channel name, 0-inactive 1-active>

// a list of unique IDs that will not exceed MAX_UNIQUEID 
list< long long> unique_id;
//list of times that each server joined
map<string, int > expiration_times;


void handle_socket_input();
void handle_login_message(void *data, struct sockaddr_in sock);
void handle_logout_message(struct sockaddr_in sock);
void handle_join_message(void *data, struct sockaddr_in sock);
void handle_leave_message(void *data, struct sockaddr_in sock);
void handle_say_message(void *data, struct sockaddr_in sock);
void handle_list_message(struct sockaddr_in sock);
void handle_who_message(void *data, struct sockaddr_in sock);
void handle_keep_alive_message(struct sockaddr_in sock);
void send_error_message(struct sockaddr_in sock, string error_msg);


void server_subscribe(void *data, struct sockaddr_in sock);
void s2s_say_request(void *data, struct sockaddr_in sock);
void s2s_leave_request(void *data, struct sockaddr_in sock);
void broadcast_join_message(string origin_server_key, char* channel);
// void soft_join(void *data, struct sockaddr_in sock);
void softcheck(int signum);
void send_leave_request(const char* channel, struct sockaddr_in sock, struct sockaddr_in server);
bool have_channel(const char* channel);
string add_user_to_channel(string channel, string username, struct sockaddr_in server, struct sockaddr_in sock);
bool server_in_channel(string channel);
bool same_port(struct sockaddr_in from, struct sockaddr_in to);
void handle_alarm(int signum);

void *get_in_addr(struct sockaddr *sa)
{
	if (sa->sa_family == AF_INET) {
		return &(((struct sockaddr_in*)sa)->sin_addr);
	}

	return &(((struct sockaddr_in6*)sa)->sin6_addr);
}

long curr_index = 0;

static void queue_id(long id) {
    
    // unique_id.push_back(id);
	list<long long>::iterator it;
	it = unique_id.begin();
	unique_id.insert(it, id);

}

long long uniqueIdGen()
{
    long long ID = 0LL;
    int fd;
    fd = open("/dev/urandom", O_RDONLY);
    read(fd, &ID, 8);
    close(fd);
    
    return ID;
}

/*
 * Verifies whether the specified ID is unique; compares the ID with all the IDs
 * inside the server's ID cache. Returns true if unique, false if not (is a duplicate,
 * indicating a loop).
 */
bool id_unique_check(long id) {

	list<long long>::iterator it;
	// it = find(unique_id.begin(), unique_id.end(), id);
	// size_t id_found = unique_id.find(id);
	for(it = unique_id.begin(); it != unique_id.end(); it++)
	{
		if( *it == id){
			// cout << "iterator: " << *it << " ID found" << id << endl;
			return false;
		}
	}
    return true;
}



void softJoin(int signal_num)
{
	//for each adjacent server
	// map<string, struct sockaddr_in>::iterator server_iter;
	// for (server_iter = servers.begin(); server_iter != servers.end(); server_iter++)
	// {	
	// 	//send a join for each subscribed channel
	// 	map<string, channel_type>::iterator channel_iter;
	// 	for (channel_iter = channels.begin(); channel_iter != channels.end(); channel_iter++)
	// 	{
	map<string, channel_type>::iterator channel_iter;
	for (channel_iter = channels.begin(); channel_iter != channels.end(); channel_iter++)
	{
		map<string, struct sockaddr_in>::iterator server_iter;
		for (server_iter = server_neighbors[channel_iter->first].begin();server_iter != server_neighbors[channel_iter->first].end(); server_iter++)
		{
			struct s2s_join s2s_join;
			ssize_t bytes;
			void *send_data;
			size_t len;
			s2s_join.s2s_type = REQ_S2S_JOIN;
			strcpy(s2s_join.s2s_channel, channel_iter->first.c_str());
			send_data = &s2s_join;
			len = sizeof s2s_join;
			if(same_port(server, server_iter->second)){
				continue;
			}
			bytes = sendto(sock_for_listenig, send_data, len, 0, (struct sockaddr*)&(server_iter->second), sizeof(server_iter->second));
			if (bytes < 0)
			{
				perror("Message failed");
			}
			else
			{
				cout << inet_ntoa(server.sin_addr) << ":" << (int)ntohs(server.sin_port) << " "
					<< inet_ntoa((server_iter->second).sin_addr) << ":" 
					<< (int)ntohs((server_iter->second).sin_port)
					<< " send S2S Join " << channel_iter->first << " (refresh)" << endl;
			}
			// }
		}
		
	}
}

void softcheck(int signum) {
    softJoin(1);
    
    // map<string, struct sockaddr_in>::iterator server_iter;
	// for (server_iter = servers.begin(); server_iter != servers.end(); server_iter++)
	// {
	// 	//this should only remove servers from server_channels map (map<channel, map<server_id string, server sockaddr_in> >)
	// 	map<string, map<string, struct sockaddr_in> >::iterator channel_iter;
	// 	for (channel_iter = server_neighbors.begin(); channel_iter != server_neighbors.end(); channel_iter++)
	// 	{
	
	map<string, channel_type>::iterator channel_iter;
	for (channel_iter = channels.begin(); channel_iter != channels.end(); channel_iter++)
	{
		map<string, struct sockaddr_in>::iterator server_iter;
		for (server_iter = server_neighbors[channel_iter->first].begin();server_iter != server_neighbors[channel_iter->first].end(); server_iter++)
		{
			string server_identifier;
			char port_str[6];
			int port = (int)ntohs((server_iter->second).sin_port);
			sprintf(port_str,"%d",port); 
			string ip = inet_ntoa((server_iter->second).sin_addr);
			
			server_identifier = ip + "." + port_str;
			string channel = channel_iter->first;

			map<string, struct sockaddr_in>::iterator find_iter;
			find_iter = server_neighbors[channel].find(server_identifier);

			cout << "Server id: " << server_identifier << " Channel name: " << channel << endl;
			if (find_iter != server_neighbors[channel].end())
			{
				// (*channel_iter).second.erase(find_iter);
				// server_neighbors[channel].erase(find_iter);
				cout << "The server I would be removing: " << find_iter->first << " from channel subscribers!" << endl;
				break;
			}      
        }
    }
}


int main(int argc, char *argv[])
{
	// char seed[sizeof(unsigned int)];
	// int devrand_fd = open("/dev/urand", O_RDONLY);   
    // read(devrand_fd, seed, sizeof(unsigned int));
    // close(devrand_fd);
    // srand((unsigned int)*seed);

	if (argc < 3 || argc % 2 != 1)
	{
		printf("Usage: ./server domain_name port_num server1_domain server1_port server2_domain server2_port ... serverN_domain serverN_port\n");
		exit(1);
	}

	char hostname[HOSTNAME_MAX];
	int port;

	strncpy(hostname, argv[1], HOSTNAME_MAX-1);
	port = atoi(argv[2]);

	//testing maps end
	struct hostent     *he;
	if ((he = gethostbyname(hostname)) == NULL) {
		puts("error resolving hostname..");
		exit(1);
	}
	struct in_addr **addr_list;
    addr_list = (struct in_addr**) he->h_addr_list;
    char serv_name[HOSTNAME_MAX];
    strcat(serv_name, inet_ntoa(*addr_list[0]));
    strcat(serv_name, ".");
    strcat(serv_name, argv[2]);
    this_server_name = serv_name;

	//create default channel Common
	// string default_channel = "Common";
	map<string,struct sockaddr_in> default_channel_users;
	channels[DEFAULT_CHANNEL] = default_channel_users;
	channel_subscriptions[DEFAULT_CHANNEL] = 1;
	
	map<string, struct sockaddr_in> server_names;
	if (argc > 3) {
        if (((argc - 3) % 2) != 0) {
            printf("Invalid number of arguments.  Usage: ./server domain_name port_num server1_domain server1_port server2_domain server2_port ... serverN_domain serverN_port\n");
        }
		for (int i = 0; i < argc - 1; i+=2) {
           
			char key[HOSTNAME_MAX+32] = {0};
			// char serv_name[HOSTNAME_MAX+32] = {0};

			if ((he = gethostbyname(argv[i+1])) == NULL){
				puts("error resolving hostname..");
				exit(1);
			}

			addr_list= (struct in_addr**) he->h_addr_list;

			strcat(key, inet_ntoa(*addr_list[0]));
			strcat(key, ".");
			strcat(key, argv[i+2]);
			string server_name = key;
			// cout << server_name << endl;

			struct sockaddr_in server_addr;
			servers[server_name] = server_addr;
			servers[server_name].sin_family = AF_INET;
			servers[server_name].sin_port = htons(atoi(argv[i+2]));
			memcpy(&servers[key].sin_addr, he->h_addr_list[0], he->h_length);

			//add name and addr_in to map of subscribers
			struct sockaddr_in server_addr_cpy;
			server_names[server_name] = server_addr_cpy;
			server_names[server_name].sin_family = AF_INET;
			server_names[server_name].sin_port = htons(atoi(argv[i+2]));
			memcpy(&server_names[key].sin_addr, he->h_addr_list[0], he->h_length);
			// cout << "server name: " << &servers[server_name] << endl;
		}
		server_neighbors[DEFAULT_CHANNEL] = server_names;
	}

	sock_for_listenig = socket(PF_INET, SOCK_DGRAM, 0);
	if (sock_for_listenig < 0){
		perror ("socket() failed\n");
		exit(1);
	}
	//struct sockaddr_in server;

	server.sin_family = AF_INET;
	server.sin_port = htons(port);

	if ((he = gethostbyname(hostname)) == NULL) {
		puts("error resolving hostname..");
		exit(1);
	}
	memcpy(&server.sin_addr, he->h_addr_list[0], he->h_length);

	int err;
	err = bind(sock_for_listenig, (struct sockaddr*)&server, sizeof server);
	if (err < 0)
	{
		perror("bind failed\n");
		exit(1);
	}

	signal(SIGALRM, handle_alarm);
	alarm(REFRESH_TIME);

	while(1) //server runs for ever
	{

		//use a file descriptor with a timer to handle timeouts
		int rc;
		fd_set receiver;
		FD_ZERO(&receiver);
		FD_SET(sock_for_listenig, &receiver);
		struct timeval max_timeout_interval;
        max_timeout_interval.tv_sec = 10;
        max_timeout_interval.tv_usec = 0;
		rc = select(sock_for_listenig+1, &receiver, NULL, NULL, NULL);

		if (rc < 0)
		{
			// printf("error in select\n");
            getchar();
		}
		else
		{
			int socket_data = 0;

			if (FD_ISSET(sock_for_listenig,&receiver))
			{
               
				//reading from socket
				handle_socket_input();
				socket_data = 1;

			}
		}
	}
	return 0;

}

void handle_socket_input()
{

	struct sockaddr_in recv_client;
	ssize_t bytes;
	void *data;
	size_t len;
	socklen_t fromlen;
	fromlen = sizeof(recv_client);
	char recv_text[MAX_MESSAGE_LEN];
	data = &recv_text;
	len = sizeof( recv_text);


	bytes = recvfrom(sock_for_listenig, data, len, 0, (struct sockaddr*)&recv_client, &fromlen);
	
	if (bytes == -1){
		perror("accept\n");
		// exit(1);   
	} 

	//maybe add case to see if it is IPv4

	if (bytes < 0)
	{
		perror ("recvfrom failed\n");
	}
	else
	{
		//printf("received message\n");

		struct request* request_msg;
		request_msg = (struct request*)data;

		//printf("Message type:");
		request_t message_type = request_msg->req_type;

		//printf("%d\n", message_type);

		if (message_type == REQ_LOGIN && bytes == 36 )
		{
			handle_login_message(data, recv_client); //some methods would need recv_client
		}
		else if (message_type == REQ_LOGOUT)
		{
			handle_logout_message(recv_client);
		}
		else if (message_type == REQ_JOIN)
		{
			handle_join_message(data, recv_client);
		}
		else if (message_type == REQ_LEAVE)
		{
			handle_leave_message(data, recv_client);
		}
		else if (message_type == REQ_SAY)
		{
			handle_say_message(data, recv_client);
		}
		else if (message_type == REQ_LIST)
		{
			handle_list_message(recv_client);
		}
		else if (message_type == REQ_WHO)
		{
			handle_who_message(data, recv_client);
		}
		else if(message_type == REQ_S2S_JOIN){
			// cout << "message type: " << message_type << endl;	//8
			server_subscribe(data, recv_client);
		}
		else if(message_type == REQ_S2S_LEAVE){
			// cout << "message type: " << message_type << endl;	//9
			s2s_leave_request(data, recv_client);
		}
		else if(message_type == REQ_S2S_SAY){
			s2s_say_request(data, recv_client);
		}
		else
		{
			//send error message to client
			send_error_message(recv_client, "*Unknown command");
		}




	}


}

void handle_login_message(void *data, struct sockaddr_in sock)
{
	struct request_login* msg;
	msg = (struct request_login*)data;

	string channel = msg->req_username;
	usernames[channel] = sock;
	// cout << "Name: " << channel << " value " << ntohs(usernames[channel].sin_port) << endl;
	active_usernames[channel] = 1;

	string ip = inet_ntoa(sock.sin_addr);
	int port = sock.sin_port;

 	char port_str[6];
 	sprintf(port_str, "%d", port);
	//cout << "port: " << port_str << endl;

	string key = ip + "." +port_str;
	//cout << "key: " << key <<endl;
	rev_usernames[key] = channel;

	cout << inet_ntoa(server.sin_addr) << ":" << (int)ntohs(server.sin_port) << " " << inet_ntoa(sock.sin_addr) << ":" << (int)ntohs(sock.sin_port) 
        << " recv Request Login " << channel << endl;

}

void handle_logout_message(struct sockaddr_in sock)
{

	//construct the key using sockaddr_in
	string ip = inet_ntoa(sock.sin_addr);
	int port = sock.sin_port;

 	char port_str[6];
 	sprintf(port_str, "%d", port);
	//cout << "port: " << port_str << endl;

	string key = ip + "." +port_str;
	//cout << "key: " << key <<endl;

	//check whether key is in rev_usernames
	map <string,string> :: iterator iter;

	/*
    for(iter = rev_usernames.begin(); iter != rev_usernames.end(); iter++)
    {
        cout << "key: " << iter->first << " username: " << iter->second << endl;
    }
	*/

	iter = rev_usernames.find(key);
	if (iter == rev_usernames.end() )
	{
		//send an error message saying not logged in
		send_error_message(sock, "Not logged in");
	}
	else
	{
		//cout << "key " << key << " found."<<endl;
		string username = rev_usernames[key];
		rev_usernames.erase(iter);

		//remove from usernames
		map<string,struct sockaddr_in>::iterator user_iter;
		user_iter = usernames.find(username);
		usernames.erase(user_iter);

		//remove from all the channels if found
		map<string,channel_type>::iterator channel_iter;
		for(channel_iter = channels.begin(); channel_iter != channels.end(); channel_iter++)
		{
			//cout << "key: " << iter->first << " username: " << iter->second << endl;
			//channel_type current_channel = channel_iter->second;
			map<string,struct sockaddr_in>::iterator within_channel_iterator;
			within_channel_iterator = channel_iter->second.find(username);
			if (within_channel_iterator != channel_iter->second.end())
			{
				channel_iter->second.erase(within_channel_iterator);
			}
		}


		//remove entry from active usernames also
		//active_usernames[username] = 1;
		map<string,int>::iterator active_user_iter;
		active_user_iter = active_usernames.find(username);
		active_usernames.erase(active_user_iter);


		cout << inet_ntoa(server.sin_addr) << ":" << (int)ntohs(server.sin_port) 
		<< " " << inet_ntoa(sock.sin_addr) << ":" << (int)ntohs(sock.sin_port) << username << " recv Request Logout" << endl;
	}

}

//check whether the user is in usernames
//if yes check whether channel is in channels
//if channel is there add user to the channel
//if channel is not there add channel and add user to the channel
void handle_join_message(void *data, struct sockaddr_in sock)
{
	
	//get message fields
	struct request_join* msg;
	msg = (struct request_join*)data;

	string channel = msg->req_channel;

	string ip = inet_ntoa(sock.sin_addr);
	int port = sock.sin_port;
 	char port_str[6];
 	sprintf(port_str, "%d", port);
	string key = ip + "." +port_str;
	cout << "Handle Join Message to: " << channel << endl;
	
	//check whether key is in rev_usernames
	map <string,string> :: iterator iter;
	iter = rev_usernames.find(key);
	if (iter == rev_usernames.end() )
	{
		//ip+port not recognized - send an error message
		send_error_message(sock, "Not logged in");
	}
	else
	{
		string username = rev_usernames[key];
		active_usernames[username] = 1;
		
		char channel_buf[CHANNEL_MAX];
		strcpy(channel_buf, channel.c_str());
		string origin_server_key = add_user_to_channel(channel, username, server, sock);


			
		cout << inet_ntoa(server.sin_addr) << ":" << (int)ntohs(server.sin_port) << " " << ip << ":" << (int)ntohs(sock.sin_port)  
				<< " RCV Request Join " << channel << " From: " << username << endl;

		broadcast_join_message(origin_server_key, channel_buf);

	}
}


void handle_leave_message(void *data, struct sockaddr_in sock)
{
	//check whether the user is in usernames
	//if yes check whether channel is in channels
	//check whether the user is in the channel
	//if yes, remove user from channel
	//if not send an error message to the user

	//get message fields
	struct request_leave* msg;
	msg = (struct request_leave*)data;
	string channel = msg->req_channel;
	string ip = inet_ntoa(sock.sin_addr);
	int port = sock.sin_port;
 	char port_str[6];
 	sprintf(port_str, "%d", port);
	string key = ip + "." +port_str;


	//check whether key is in rev_usernames
	map <string,string> :: iterator iter;


	iter = rev_usernames.find(key);
	if (iter == rev_usernames.end() )
	{	// check if the user is logged in
		//ip+port not recognized - send an error message
		send_error_message(sock, "Not logged in");
	}
	else
	{	//check if channel is in channels
		string username = rev_usernames[key];
		map<string,channel_type>::iterator channel_iter;
		channel_iter = channels.find(channel);
		active_usernames[username] = 1;
		if (channel_iter == channels.end())
		{
			//channel not found
			send_error_message(sock, "No channel by the name " + channel);
			cout << "server: " << username << " trying to leave non-existent channel " << channel << endl;

		}
		else
		{
			//channel already exits
			//map<string,struct sockaddr_in> existing_channel_users;
			//existing_channel_users = channels[channel];
			map<string,struct sockaddr_in>::iterator channel_user_iter;
			channel_user_iter = channels[channel].find(username);

			if (channel_user_iter == channels[channel].end())
			{
				//user not in channel
				send_error_message(sock, "You are not in channel " + channel);
				cout << "server: " << username << " trying to leave channel " << channel  << " where he/she is not a member" << endl;
			}
			else
			{
				channels[channel].erase(channel_user_iter);
				//existing_channel_users.erase(channel_user_iter);
				cout << "server: " << username << " leaves channel " << channel <<endl;

				//delete channel if no more users
				if (channels[channel].empty() && (channel != "Common") )
				{
					channels.erase(channel_iter);
					channel_subscriptions.erase(channel);
					cout <<  inet_ntoa(server.sin_addr) 
                        << ":" << (int)ntohs(server.sin_port) 
                        << "server: " << "removing empty channel " << channel <<endl;
				}
			}
		}
	}
}


void handle_say_message(void *data, struct sockaddr_in sock)
{
	//check whether the user is in usernames
	//if yes check whether channel is in channels
	//check whether the user is in the channel
	//if yes send the message to all the members of the channel
	//if not send an error message to the user

	//get message fields
	struct request_say* msg;
	msg = (struct request_say*)data;
	string channel = msg->req_channel;
	string text = msg->req_text;
	string ip = inet_ntoa(sock.sin_addr);
	int port = sock.sin_port;
 	char port_str[6];
 	sprintf(port_str, "%d", port);
	string key = ip + "." +port_str;

	
	//check whether key is in rev_usernames
	map <string,string> :: iterator iter;
	iter = rev_usernames.find(key);
	if (iter == rev_usernames.end() )
	{
		//ip+port not recognized - send an error message
		send_error_message(sock, "Not logged in ");
	}
	else
	{
		string username = rev_usernames[key];
		active_usernames[username] = 1;

		map<string,channel_type>::iterator channel_iter;
		channel_iter = channels.find(channel);
		
		if (channel_iter == channels.end())
		{
			//channel not found
			send_error_message(sock, "No channel by the name " + channel);
			cout << inet_ntoa(server.sin_addr) << ":" << (int)ntohs(server.sin_port) 
                << " " << inet_ntoa(sock.sin_addr) << ":" << (int)ntohs(sock.sin_port) 
                << " " << username  << " trying to send a message to non-existent channel " << channel << endl;
		}
		else
		{
			//channel already exits
			map<string,struct sockaddr_in>::iterator channel_user_iter;
			channel_user_iter = channels[channel].find(username);

			if (channel_user_iter == channels[channel].end())
			{
				//user not in channel
				send_error_message(sock, "You are not in channel " + channel);
				cout << inet_ntoa(server.sin_addr) << ":" << (int)ntohs(server.sin_port) 
                << " " << inet_ntoa(sock.sin_addr) << ":" << (int)ntohs(sock.sin_port) 
                << " " << username << " trying to send a message to channel " << channel  << " where he/she is not a member" << endl;
			}
			else
			{
				// map<string,struct sockaddr_in> existing_channel_users;
				// existing_channel_users = channels[channel];

				for(channel_user_iter = channels[channel].begin(); channel_user_iter != channels[channel].end(); channel_user_iter++)
				{
					//cout << "key: " << iter->first << " username: " << iter->second << endl;
					ssize_t bytes;
					void *send_data;
					size_t len;
					struct text_say send_msg;
					send_msg.txt_type = TXT_SAY;

					strncpy(send_msg.txt_channel, channel.c_str(), (CHANNEL_MAX));
					strncpy(send_msg.txt_username, username.c_str(), (USERNAME_MAX));
					strncpy(send_msg.txt_text, text.c_str(), (SAY_MAX));

					send_data = &send_msg;
					len = sizeof( send_msg);

					struct sockaddr_in send_sock = channel_user_iter->second;
					bytes = sendto(sock_for_listenig, send_data, len, 0, (struct sockaddr*)&send_sock, sizeof(send_sock));
					if (bytes < 0) {
						perror("Message failed\n"); //error
					}
				}
				//change this
				cout << inet_ntoa(server.sin_addr) << ":" << (int)ntohs(server.sin_port) 
                << " " << inet_ntoa(sock.sin_addr) << ":" << (int)ntohs(sock.sin_port) 
                << " Server: recv Request Say " << channel << " \"" << text << "\"" << endl;
				
				
				long long unique_id_gen = uniqueIdGen();
				queue_id(unique_id_gen);
				// cout << "ID for this channel: " << unique_id_gen << endl;
				
				// map<string, struct sockaddr_in> existing_channel_servers = server_neighbors[channel];
                map<string, struct sockaddr_in>::iterator server_iter;
                for (server_iter = server_neighbors[channel].begin(); server_iter != server_neighbors[channel].end(); server_iter++)
                {
					
					ssize_t bytes;
					void *send_data;
					size_t len;
					// put together our s2s message to broadcast
					struct s2s_say s_packet;					
					
					memset(&s_packet, '\0', sizeof(s_packet));
					strcpy(s_packet.s2s_channel, channel.c_str());
					strcpy(s_packet.s2s_username, username.c_str());
					strncpy(s_packet.s2s_text, text.c_str(), SAY_MAX);
					
					// for(int j = 0; j< 64; j++){
					// if(id_unique_check(unique_id_gen)== 1){
						//copy in the ID
					s_packet.s2s_uniqueID = unique_id_gen;
					// }

					s_packet.s2s_type = REQ_S2S_SAY;

					send_data = &s_packet;
					len = sizeof(s_packet);

					//cout << username <<endl;
					struct sockaddr_in recv_sock = server_iter->second;
					if (!same_port(server, recv_sock)){
						bytes = sendto(sock_for_listenig, send_data, len, 0, (struct sockaddr*)&recv_sock, sizeof(recv_sock));
						if (bytes < 0)
						{
							perror("Message failed\n"); //error
						}else
						{
							cout << inet_ntoa(server.sin_addr) << ":" << (int)ntohs(server.sin_port) 
							<< " " << inet_ntoa(recv_sock.sin_addr) << ":" << (int)ntohs(recv_sock.sin_port) << " send S2S Request Say " << channel << " " << username << endl;
						}
					}
				}
			}
		}
	}
}


void handle_list_message(struct sockaddr_in sock)
{

	//check whether the user is in usernames
	//if yes, send a list of channels
	//if not send an error message to the user
	string ip = inet_ntoa(sock.sin_addr);

	int port = sock.sin_port;

 	char port_str[6];
 	sprintf(port_str, "%d", port);
	string key = ip + "." +port_str;

	//check whether key is in rev_usernames
	map <string,string> :: iterator iter;
	iter = rev_usernames.find(key);
	if (iter == rev_usernames.end() )
	{
		//ip+port not recognized - send an error message
		send_error_message(sock, "Not logged in ");
	}
	else
	{
		string username = rev_usernames[key];
		int size = channels.size();
		//cout << "size: " << size << endl;

		active_usernames[username] = 1;

		ssize_t bytes;
		void *send_data;
		size_t len;
		//struct text_list temp;
		struct text_list *send_msg = (struct text_list*)malloc(sizeof (struct text_list) + (size * sizeof(struct channel_info)));
		send_msg->txt_type = TXT_LIST;
		send_msg->txt_nchannels = size;
		map<string,channel_type>::iterator channel_iter;

		//struct channel_info current_channels[size];
		//send_msg.txt_channels = new struct channel_info[size];
		int pos = 0;
		for(channel_iter = channels.begin(); channel_iter != channels.end(); channel_iter++)
		{
			string current_channel = channel_iter->first;
			const char* str = current_channel.c_str();
			//cout << "channel " << str <<endl;
			strcpy(((send_msg->txt_channels)+pos)->ch_channel, str);
			//strcpy(((send_msg->txt_channels)+pos)->ch_channel, "hello");
			//cout << ((send_msg->txt_channels)+pos)->ch_channel << endl;

			pos++;

		}



		//send_msg.txt_channels =
		//send_msg.txt_channels = current_channels;
		send_data = send_msg;
		len = sizeof (struct text_list) + (size * sizeof(struct channel_info));

					//cout << username <<endl;
		struct sockaddr_in send_sock = sock;


		//bytes = sendto(s, send_data, len, 0, (struct sockaddr*)&send_sock, fromlen);
		bytes = sendto(sock_for_listenig, send_data, len, 0, (struct sockaddr*)&send_sock, sizeof send_sock);

		if (bytes < 0)
		{
			perror("Message failed\n"); //error
		}
		else
		{
			//printf("Message sent\n");

		}

		cout << "server: " << username << " lists channels"<<endl;


	}



}


void handle_who_message(void *data, struct sockaddr_in sock)
{
	//check whether the user is in usernames
	//if yes check whether channel is in channels
	//if yes, send user list in the channel
	//if not send an error message to the user

	//get message fields
	struct request_who* msg;
	msg = (struct request_who*)data;

	string channel = msg->req_channel;

	string ip = inet_ntoa(sock.sin_addr);

	int port = sock.sin_port;

 	char port_str[6];
 	sprintf(port_str, "%d", port);
	string key = ip + "." +port_str;


	//check whether key is in rev_usernames
	map <string,string> :: iterator iter;


	iter = rev_usernames.find(key);
	if (iter == rev_usernames.end() )
	{
		//ip+port not recognized - send an error message
		send_error_message(sock, "Not logged in ");
	}
	else
	{
		string username = rev_usernames[key];

		active_usernames[username] = 1;

		map<string,channel_type>::iterator channel_iter;

		channel_iter = channels.find(channel);

		if (channel_iter == channels.end())
		{
			//channel not found
			send_error_message(sock, "No channel by the name " + channel);
			cout << "server: " << username << " trying to list users in non-existing channel " << channel << endl;

		}
		else
		{
			//channel exits
			map<string,struct sockaddr_in> existing_channel_users;
			existing_channel_users = channels[channel];
			int size = existing_channel_users.size();

			ssize_t bytes;
			void *send_data;
			size_t len;

			//struct text_list temp;
			struct text_who *send_msg = (struct text_who*)malloc(sizeof (struct text_who) + (size * sizeof(struct user_info)));
			send_msg->txt_type = 2;
			send_msg->txt_nusernames = size;
			const char* str = channel.c_str();
			strncpy(send_msg->txt_channel, str, (CHANNEL_MAX-1));
			map<string,struct sockaddr_in>::iterator channel_user_iter;

			int pos = 0;

			for(channel_user_iter = existing_channel_users.begin(); channel_user_iter != existing_channel_users.end(); channel_user_iter++)
			{
				string username = channel_user_iter->first;
				str = username.c_str();
				strcpy(((send_msg->txt_users)+pos)->us_username, str);
				pos++;
			}

			send_data = send_msg;
			len = sizeof(struct text_who) + (size * sizeof(struct user_info));
						//cout << username <<endl;
			struct sockaddr_in send_sock = sock;

			//bytes = sendto(s, send_data, len, 0, (struct sockaddr*)&send_sock, fromlen);
			bytes = sendto(sock_for_listenig, send_data, len, 0, (struct sockaddr*)&send_sock, sizeof send_sock);

			if (bytes < 0)
			{
				perror("Message failed\n"); //error
			}
			else
			{
				//printf("Message sent\n");

			}

			cout << "server: " << username << " lists users in channnel "<< channel << endl;
		}
	}
}


void send_error_message(struct sockaddr_in sock, string error_msg)
{
	ssize_t bytes;
	void *send_data;
	size_t len;

	struct text_error send_msg;
	send_msg.txt_type = TXT_ERROR;

	const char* str = error_msg.c_str();
	strncpy(send_msg.txt_error, str, (SAY_MAX - 1));

	send_data = &send_msg;

	len = sizeof send_msg;


	struct sockaddr_in send_sock = sock;



	bytes = sendto(sock_for_listenig, send_data, len, 0, (struct sockaddr*)&send_sock, sizeof send_sock);

	if (bytes < 0)
	{
		perror("Message failed\n"); //error
	}
	else
	{
		//printf("Message sent\n");

	}


}


void server_subscribe(void *data, struct sockaddr_in sock){
	// cout << " joining another server " << endl;
	struct s2s_join *join_msg;
	join_msg = (struct s2s_join*) data;
	char channel[CHANNEL_MAX];
    strcpy(channel, join_msg->s2s_channel);

	string ip = inet_ntoa(sock.sin_addr);
    int srcport = htons(sock.sin_port);
	char port_str[6];
	sprintf(port_str, "%d", srcport);
	string key = ip + "." + port_str;

	map<string,int>::iterator subscription_iter;
	subscription_iter = channel_subscriptions.find(channel);

	if(subscription_iter == channel_subscriptions.end()){
		channel_subscriptions[channel] = 1;
		cout << inet_ntoa(server.sin_addr) << ":" << int(ntohs(server.sin_port)) << " " << ip << ":" << (int)ntohs(sock.sin_port)
	    << " recv S2S Join " << channel << " (forwarding)" << endl;

		map<string, struct sockaddr_in>::iterator server_iter;
		map<string, struct sockaddr_in> server_names;
		for(server_iter = servers.begin(); server_iter !=servers.end(); server_iter++){

			server_names[server_iter->first] = server_iter->second;

		}
		server_neighbors[channel] = server_names;
		// expiration_times[server_iter->first] = time(0) + 2* REFRESH_TIME;

		broadcast_join_message(key, channel);

	}else{
		//channel found,
        //search the map of timers on this channel for the timer
		// channels[channel] = sock;
		map<string, struct sockaddr_in> server_names;
		map<string, struct sockaddr_in>::iterator server_iter;
        for (server_iter = servers.begin(); server_iter != servers.end(); server_iter++)
        {
            // if (key == server_iter->first)
            // {
				// expiration_times[server_iter->first] = time(0) + 2* REFRESH_TIME;
			server_names[server_iter->first] = server_iter->second;
			// (server_neighbors[channel])[server_iter->first] = server_iter->second;
			// }
			
		}
		server_neighbors[channel] = server_names;
		

        cout << inet_ntoa(server.sin_addr) << ":" << (int)ntohs(server.sin_port)
            << " " << inet_ntoa(sock.sin_addr) << ":" << (int)ntohs(sock.sin_port)
            << " recv S2S Join " << channel << " (forwarding ends here)" << endl;
	}
	// map<string, struct sockaddr_in>::iterator server_iter;
	// for (server_iter = servers.begin(); server_iter != servers.end(); server_iter++)
	// {
		cout << "Set expr time for " << key << endl;
	expiration_times[key] = time(0) + 2 * REFRESH_TIME;
	// }
}


			// map<string, struct sockaddr_in>::iterator server_it;
			// // if(server_neighbors.find(channel) != server_neighbors.end()){
			// for(server_it = server_neighbors[channel].begin(); server_it != server_neighbors[channel].end(); server_it++){
			// 	/* Reply to sender with S2S if duplicate, loop detected */
			// 	if (key == server_it->first && same_port(server, server_it->second)) {
			// 	if(!server_in_channel(server_it->first, server_it->second) && !same_port(sock, server_it->second)){
			// 		send_leave_request(channel, server_it->second, server);
			// 		ssize_t ret;
			// 		size_t len;
			// 		struct s2s_leave s2s_leave;
			// 		s2s_leave.s2s_type = REQ_S2S_LEAVE;
			// 		strcpy(s2s_leave.s2s_channel, channel);
			// 		len = sizeof( s2s_leave);
			// 		ret = sendto(sock_for_listenig, &s2s_leave, len, 0, (struct sockaddr*)(&server_it->second), sizeof(server_it->second));
			// 		if (ret == -1){
			// 			perror("sendto fail");
			// 		}else{
			// 			cout << inet_ntoa(server.sin_addr) << ":" << (int)ntohs(server.sin_port)
			// 				<< " " << inet_ntoa((server_it->second).sin_addr) << ":"
			// 				<< (int)ntohs((server_it->second).sin_port) << " send S2S Leave(2) " << channel << endl;
			// 		}
			// 	}				
			// 		// break;
			// 	}
			// }


void s2s_say_request(void *data, struct sockaddr_in sock){
	// cout << " rcv: trying to say something to a server..." << endl;
	struct s2s_say *server_say_packet; 
	server_say_packet = (struct s2s_say*) data;
	long long unique_id_from_data = server_say_packet->s2s_uniqueID;
	// for(int i =0; i < unique_id.size(); i++){
	// cout << "From server: " << (int)ntohs(server.sin_port) << " ID: " << unique_id_from_data << endl;
	// }
	char channel[CHANNEL_MAX];
	char username[USERNAME_MAX];
    char text[SAY_MAX];
    strncpy(channel, server_say_packet->s2s_channel, CHANNEL_MAX);
	strncpy(username, server_say_packet->s2s_username,USERNAME_MAX);
	strncpy(text, server_say_packet->s2s_text,SAY_MAX);
	
	string ip = inet_ntoa(sock.sin_addr);
    int srcport = (int)ntohs(sock.sin_port);
    char port_str[6];
    sprintf(port_str, "%d", srcport);
    string key = ip + "." + port_str;

	channels[channel][username] = sock;

	cout << inet_ntoa(server.sin_addr) << ":" << (int)ntohs(server.sin_port) << " " << inet_ntoa(sock.sin_addr)  << ":" << ntohs(sock.sin_port) 
        << " recv S2S Say (1) " << username << " " << channel << "\"" << text << "\"" << endl;
	
	// // Check the packet ID for uniqueness
	bool message_seen=false;
	list<long long>::iterator id_it;
	for(id_it= unique_id.begin(); id_it != unique_id.end(); id_it++)
	{
		if(*id_it == server_say_packet->s2s_uniqueID){ //id is found
			// dealing with nearby servers
			cout <<" iterater ID " << *id_it << " ID from data: " << server_say_packet->s2s_uniqueID << endl;
			message_seen = true;
		}
	}
	if(message_seen){
		send_leave_request(channel, sock, server);
		return;
	}

	// if we have gotten here, it means that we have not yet seen this uniqueID
    // so add it to the list
	queue_id(unique_id_from_data);
	bool should_leave = true;
	// dealing with clients 
	map<string, struct sockaddr_in>::iterator client_it;
	for(client_it = channels[channel].begin(); client_it != channels[channel].end(); client_it++){
		ssize_t ret;
		struct text_say send_msg;
		send_msg.txt_type= TXT_SAY;
		
		void *send_data;
		strcpy(send_msg.txt_channel, channel);
		strcpy(send_msg.txt_username, username);
		strcpy(send_msg.txt_text, text);
		send_data = &send_msg;
		
		struct sockaddr_in send_sock = client_it->second;
		string curr_user = client_it->first;
		// cout << " we want to find: " << " " << ntohs(client_it->second.sin_port) << " " << curr_user << endl;
		// map<string,struct sockaddr_in>::iterator channel_user_iter;
		// channel_user_iter = channels[channel].find(username);
		
		// 
		if( usernames.find(curr_user) != usernames.end() ){
			// cout << "found username: " << curr_user << " on server " << ntohs(send_sock.sin_port) << endl;
			ret = sendto(sock_for_listenig, send_data, sizeof(send_msg), 0, (struct sockaddr*)&send_sock, sizeof( send_sock));
			if (ret == -1){
				perror("sendto fail");
			}
			// cout << username << " pritning message for " << (int)ntohs(send_sock.sin_port) <<  " " << channel << endl;
			should_leave = false; 
		}else{
			// cout << "NOT found username: " << curr_user << " on server " << ntohs(send_sock.sin_port) << endl;
		}
	}
	
    //------------------------------------ FIX-----------------------------------------
	int server_subscribers = 0;
    map<string,struct sockaddr_in>::iterator server_iter;
    for (server_iter = server_neighbors[channel].begin(); server_iter != server_neighbors[channel].end(); server_iter++)
    {
        if (key != server_iter->first) {
            server_subscribers++;
        }
    }
    //cout << "server subs = " << server_subscribers << endl;
    if (server_subscribers > 0)
    {
        //forward message to each server
        ssize_t bytes;
        size_t len;
        void *send_data;
        struct s2s_say s2s_say;
        //building s2s say packet
        s2s_say.s2s_type = REQ_S2S_SAY;
        strcpy(s2s_say.s2s_username, username);
        strcpy(s2s_say.s2s_channel, channel);
        strcpy(s2s_say.s2s_text, text);
        s2s_say.s2s_uniqueID = unique_id_from_data;

        send_data = &s2s_say;
        len = sizeof s2s_say;
        //forward to all  REMAINING interfaces (not sender)
        for (server_iter = server_neighbors[channel].begin(); server_iter != server_neighbors[channel].end(); server_iter++)
        {
            //if server is not sender
			// cout << "Check send to server: " << (int)ntohs(server.sin_port) << " Sock:  " << (int)ntohs(sock.sin_port) << " Server Iter: " << (int)ntohs(server_iter->second.sin_port) << endl;
            if ( !same_port(server, server_iter->second) && key != server_iter->first && !same_port(sock, server_iter->second)){
				// if(server_in_channel(server_iter->first, server_iter->second)){
				bytes = sendto(sock_for_listenig, send_data, len, 0, (struct sockaddr*)&server_iter->second, sizeof( server_iter->second));
				if (bytes < 0){
					perror("Message failed");
				}else{
					cout << inet_ntoa(server.sin_addr) << ":" << (int)ntohs(server.sin_port)
					<< " " << inet_ntoa(sock.sin_addr) << ":" << (int)ntohs(server_iter->second.sin_port)
					<< " send S2S Request Say " << username << " " << channel << " \"" << text << "\"" << endl;
				}
            }else{
				// cout<< should_leave << " Channel " << channel << " from " << server_iter->first << " and " << server.sin_port << endl;
				if(should_leave && !same_port(server, server_iter->second) ){
					send_leave_request(channel, server_iter->second, server);
				}
			}
        }
    }
    else // server_subscribers = 0
    {
        if (channels[channel].empty() && (strcmp(channel, DEFAULT_CHANNEL)!=0))
        {
            //cout << "Channel is empty on this server" << endl;
            ssize_t bytes;
            size_t len;
            void *send_data;
            struct s2s_leave s2s_leave;
            s2s_leave.s2s_type = REQ_S2S_LEAVE;
            strcpy(s2s_leave.s2s_channel, channel);
            send_data = &s2s_leave;
            len = sizeof s2s_leave;
            bytes = sendto(sock_for_listenig, send_data, len, 0, (struct sockaddr*)&sock, sizeof sock);
            if (bytes < 0)
            {
                perror("Message failed");
            }
            else
            {
            cout << inet_ntoa(server.sin_addr) << ":" << (int)htons(server.sin_port)
               << " " << inet_ntoa(sock.sin_addr) <<":"<< (int)htons(sock.sin_port)
               << " send S2S Leave(3) " << channel << endl;
            }
        }
    }

}

void s2s_leave_request(void *data, struct sockaddr_in sock){
	struct s2s_leave* msg;
	msg = (struct s2s_leave*)data;
	string channel = msg->s2s_channel;
	string ip = inet_ntoa(sock.sin_addr);
	int port = ntohs(sock.sin_port);
 	char port_str[6];
 	sprintf(port_str, "%d", port);
	//key = localhost.500*
	string key = ip + "." + port_str;

	map<string,struct sockaddr_in>::iterator server_iter;

	server_iter = server_neighbors[channel].find(key);
	if (server_iter != server_neighbors[channel].end())
	{
		// channels[channel].erase(server_iter->first);
		// usernames.erase(server_iter->first);
		server_neighbors[channel].erase(server_iter);
		
		cout << inet_ntoa(server.sin_addr) << ":" << (int)ntohs(server.sin_port)
        << " " << inet_ntoa(server_iter->second.sin_addr) << ":" << (int)ntohs(server_iter->second.sin_port)
        << " recv S2S Leave " << channel << endl;
    }

}

void broadcast_join_message(string origin_server_key, char* channel){
	ssize_t bytes;
    void *send_data;
    size_t len;
    // map<string,struct sockaddr_in>::iterator server_iter;
	// cout << "In Broadcast join message " << endl;
    struct s2s_join join_msg;
    join_msg.s2s_type = REQ_S2S_JOIN;
    strncpy(join_msg.s2s_channel, channel, CHANNEL_MAX);

    send_data = &join_msg;
    len = sizeof join_msg;
	map<string, struct sockaddr_in>::iterator find_iter;
	for(find_iter = servers.begin(); find_iter != servers.end(); find_iter++)
    {	
		// cout << "Find itter in broadcast " << find_iter->first << " origin server key: " << origin_server_key << endl;
        if (origin_server_key != find_iter->first)
        {
			if (!same_port(server, find_iter->second)){
				// if (sock.sin_addr.s_addr == (find_iter->second).sin_addr.s_addr && ntohs(sock.sin_port) == ntohs(find_iter->second.sin_port))
				// cout<< " "<< find_iter->first << " != " << origin_server_key << endl;
			
				bytes = sendto(sock_for_listenig, send_data, len, 0, (struct sockaddr*)(&find_iter->second), sizeof(find_iter->second));
				if (bytes < 0){
					perror("Message failed\n");
				}else{
					cout << inet_ntoa(server.sin_addr) << ":" << (int)ntohs(server.sin_port)
						<< " " << inet_ntoa(find_iter->second.sin_addr) << ":" 
						<< (int)ntohs(find_iter->second.sin_port) << " send S2S Request Join " << channel << endl;
				}
			}else{
				// cout << "Origin key and channel matched. Not forwarding back to sender." << endl;
			}
		}
    }
}

//0 is false
bool same_port(struct sockaddr_in from, struct sockaddr_in to){
	// cout << ((inet_ntoa(from.sin_addr) == inet_ntoa(to.sin_addr)) && ((from.sin_port) == (to.sin_port))) << endl;
	return ((inet_ntoa(from.sin_addr) == inet_ntoa(to.sin_addr)) && ((from.sin_port) == (to.sin_port)));
}

string add_user_to_channel(string channel, string username, struct sockaddr_in server, struct sockaddr_in sock){
	
	map<string, channel_type>::iterator channel_iter;
	channel_iter = channels.find(channel);
	char port_str[6];
	string domain_name = inet_ntoa(server.sin_addr);
	sprintf(port_str, "%d", (int)ntohs(server.sin_port));
	string origin_server_key = domain_name + "." + port_str;

	if (channel_iter == channels.end() ){
		//channel not found
		cout << "New channel didn't exist" << endl;
		map<string,struct sockaddr_in> new_channel_users;
		new_channel_users[username] = sock;
		channels[channel] = new_channel_users;
		
		channel_subscriptions[channel] = 1;
		
		char channel_buf[CHANNEL_MAX];
		strcpy(channel_buf, channel.c_str());
	
		//loop through servers
		map<string, struct sockaddr_in>::iterator server_itr;
		map<string, struct sockaddr_in> server_names;
		// map<string, server_typen>::iterator server_itr; //for neighbors
		int count = 0;
		for(server_itr = servers.begin(); server_itr != servers.end(); server_itr++){
			count++;
			server_names[server_itr->first] = server_itr->second;
		}
		server_neighbors[channel] = server_names;
	}else{
		
		// add the user to the channels
		map<string, struct sockaddr_in> new_channel_users;
		new_channel_users[username] = sock;
		channels[channel] = new_channel_users;
        
		// channel already exits
		// map<string,struct sockaddr_in>* existing_channel_users;
		// existing_channel_users = &channels[channel];
		// *existing_channel_users[username] = sock;
		cout << "Name: " << username << " Channel name: " << channel_iter->first << endl;
		channels[channel][username] = sock;
		//cout << "joining exisitng channel" << endl;
	}
	return origin_server_key;

}

bool have_channel(const char* channel){
	map<string, struct sockaddr_in>::iterator neighbor_iter;
	for(neighbor_iter = server_neighbors[channel].begin(); neighbor_iter != server_neighbors[channel].end(); neighbor_iter++){
		if(neighbor_iter->first == channel){
			return true;
		}
	}
	return false;
}

void send_leave_request(const char* channel, struct sockaddr_in sock, struct sockaddr_in server){
	ssize_t ret;
	struct s2s_leave leave_msg;
	leave_msg.s2s_type = REQ_S2S_LEAVE;
	strncpy(leave_msg.s2s_channel, channel, CHANNEL_MAX);

	//erase server from channel list?
	// channels.erase(server_say_packet->s2s_channel);
	// if(same_port(sock, server)){
	// 	return;
	// }
	ret = sendto(sock_for_listenig, &leave_msg, sizeof(leave_msg), 0, (struct sockaddr*)&sock, sizeof(sock));
	if(ret ==-1){
		perror("sendto failed");
	}else{
		cout << inet_ntoa(server.sin_addr) << ":" << (int)ntohs(server.sin_port)
		<< " " << inet_ntoa((sock).sin_addr) << ":"
		<< (int)ntohs((sock).sin_port) << " send S2S Leave(2) " << channel << endl;
	}
	return;
}

bool server_in_channel(string channel){
	map< string, map<string, struct sockaddr_in> >::iterator neighbor_iter;
	for(neighbor_iter = server_neighbors.begin(); neighbor_iter != server_neighbors.end(); neighbor_iter++){
		if(neighbor_iter->first == channel){
			return true;
		}
	}
	return false;
}

int check_expirations(){
	int now = time(0);
	int next_expir = (2 * REFRESH_TIME);
	map<string, int>::iterator refresh_iter;
	map<string, map<string, struct sockaddr_in> >::iterator neighbor_iter;
	
	for(refresh_iter = expiration_times.begin(); refresh_iter != expiration_times.end(); refresh_iter++){
		// cout << "Checking " << refresh_iter->first <<" exp time " << refresh_iter->second << " now " << now << endl;
		if(refresh_iter->second <= now){
			
			for(neighbor_iter = server_neighbors.begin(); neighbor_iter != server_neighbors.end(); neighbor_iter++){
				
				map<string, struct sockaddr_in>::iterator neighbor_map_iter;
				
				for(neighbor_map_iter = server_neighbors[neighbor_iter->first].begin(); neighbor_map_iter != server_neighbors[neighbor_iter->first].end(); neighbor_map_iter++){
					// cout << " within map " << neighbor_map_iter->first << endl;
					// cout << "Found neighbor to expire: " << refresh_iter->first << endl;
					// server_neighbors[refresh_iter->first].erase(neighbor_iter->first);
					char channel[CHANNEL_MAX];
					strcpy(channel, neighbor_iter->first.c_str());
					send_leave_request(channel, neighbor_map_iter->second, server);
				}
			}
			
		}else{
			int curr_expir = refresh_iter->second - now;
			if(curr_expir < next_expir && curr_expir > 0){
				next_expir = curr_expir;
			}
		}
	}
	return next_expir;

}
//handled refresh
void handle_alarm(int signum){
	int now = time(0);
	int next_refresh_interval = (last_refresh_time + REFRESH_TIME) - now;
	int next_alarm = 0;

	if(next_refresh_interval <= 0){
		softJoin(1);
		last_refresh_time = now;
		next_alarm = REFRESH_TIME;
	}else{
		next_alarm = next_refresh_interval;
	}
	int next_expiration = check_expirations();
	if(next_expiration < next_alarm){
		next_alarm = next_expiration;
	}
	// cout << "setting alarm to: " << next_alarm << endl;
	alarm(next_alarm);
}