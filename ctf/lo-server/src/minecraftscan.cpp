#include <iostream>
#include <string>
#include <cstring>
#include <vector>
#include <sys/types.h>
#include <sys/socket.h>
#include <netdb.h>
#include <fcntl.h>
#include <errno.h>
#include <poll.h>
#include <unistd.h>
#include <fstream>
#include <sstream>
#include <signal.h>
#include <algorithm>
#include <execution>


/**
* Tool to scan a given list of hosts to determine if they are Minecraft servers,
* and to gather some basic information about them if they are.
*/

#define PROCESS_COUNT 20

// Timeout for poll calls
#define POLL_TIMEOUT 30000

// VarInt constants
#define SEGMENT_BITS 0x7F
#define CONTINUE_BIT 0x80

// The port to connect to
const char *port = "25565";

// Vector to store the hosts (IP addresses or domain names)
std::vector<const char*> hosts;

// How many hosts have we scanned so far
int hosts_scanned = 0;

/**
 * Parses a VarInt and increments pointer past it
 * 
 * Yoinked from https://wiki.vg/Protocol#VarInt_and_VarLong
 * 
 * @param p points to the beginning of the VarInt
 * @param len how much longer is the memory valid past `p`
 * @param ok did the decode succeed?
*/
int parseVarInt(char* & p, int len, bool & ok) {
    int value = 0;
    int position = 0;
    char currentByte;
	char *p_initial = p;


    while (true) {
		if(p - p_initial >= len){ // Make sure we aren't reading past the buffer
			// We overran the memory
			std::cerr << "VarInt overran memory" << std::endl;
			ok = false;
			return -1;
		}

		// Yoink
		
        currentByte = *(p++);
        value |= (currentByte & SEGMENT_BITS) << position;

        if ((currentByte & CONTINUE_BIT) == 0) break;

        position += 7;

        if (position >= 32){
			std::cerr << "VarInt too large!" << std::endl;
			ok = false;
			return -1;
		}
    }
	ok = true;
    return value;
}

/**
 * Constructs a String object
 * 
 * @param str the string to be packed
 * @return the string length as a VarInt, then the UTF-8 string
*/
std::vector<char> constructString(std::string str){
	// Grab the characters out into a vector
	std::vector<char> body;
	for(char c: str){
		body.push_back(c);
	}
	// Return that vector, prepended with the size
	std::vector<char> ret;
	ret.push_back((char) body.size());
	ret.insert(ret.end(), body.begin(), body.end());
	return ret;
}

/**
 * Updates the progress message
*/
void updateMessage(){
	std::cout << "\rScanning... " << ((float) hosts_scanned/(float) hosts.size()) * 100 << "% done (" << hosts_scanned << " of " << hosts.size() << ")       " << std::flush;
}

void incrementHostsScanned(){
	hosts_scanned++;
}

void search(int & sockfd, const char* host, std::string & json){
	// Load address structs with getaddrinfo()
	struct addrinfo hints, *res;
	memset(&hints, 0, sizeof hints);
	hints.ai_family = AF_UNSPEC;
	hints.ai_socktype = SOCK_STREAM;

	getaddrinfo(host, port, &hints, &res);

	// Open the socket
	sockfd = socket(res->ai_family, res->ai_socktype, res->ai_protocol);
	fcntl(sockfd, F_SETFL, O_NONBLOCK);

	// Connect to the socket
	if(connect(sockfd, res->ai_addr, res->ai_addrlen) == -1){
		if(errno == EINPROGRESS){ // If the socket isn't immediately connected,
			std::cerr << "EINPROGRESS, will poll" << std::endl;
			// Poll the socket until it is connected (or until we time out)
			constexpr int num_pfds = 1;
			struct pollfd pfds[num_pfds];
			pfds[0].fd = sockfd;
			pfds[0].events = POLLOUT;

			int num_events = poll(pfds, num_pfds, POLL_TIMEOUT);
			// If we get an error or time out, report it and move on to the next host
			if(num_events < 0){
				std::cerr << "Connection error when polling, errno = " << errno << std::endl;
				return;
			}
			if(num_events == 0){ 
				std::cerr << "Connection timed out" << std::endl;
				return;
			}
			std::cerr << "Poll done, got connection maybe. num_events is " << num_events << std::endl;
		}
		if(errno != EINPROGRESS){ // After we're done, or if EINPROGRESS was never the errno, check for actual errors
			std::cerr << "Connect error. Errno = " << errno << std::endl;
			return;
		}
	}

	// Handshake packet

	bool error = false;
	
	std::vector<char> handshake_packet = {
		0,                             									// Length (VarInt) will set later, assume it isn't longer than 255
		0x00,                          									// Packet ID (VarInt)
		'\xf8', '\x05'                     								// Protocol Version (VarInt)
	};
	auto host_string_in_packet_form = constructString(host);			// Host string
	handshake_packet.insert(handshake_packet.end(),						// ...
		host_string_in_packet_form.begin(),								// ...
		host_string_in_packet_form.end()								// ...
		);
	unsigned short port_i = (unsigned short) strtoul(port, nullptr, 0);	// Port
	handshake_packet.push_back(((port_i & 0xff00) >> 8) | 0x80); 		// ...
	handshake_packet.push_back((port_i & 0x00ff));	  					// ...
	handshake_packet.push_back(0x01); 									// Next state
	handshake_packet[0] = handshake_packet.size() - 1; // Set the size, the size excludes itself so minus one
	
	// Send handshake packet
	int handshake_len = handshake_packet.size();
	int handshake_sent = send(sockfd, &(handshake_packet[0]), handshake_len, 0);
	while(handshake_sent < handshake_len){
		int handshake_send_ret = send(sockfd, &(handshake_packet[0]), handshake_len - handshake_sent, 0);
		if(handshake_send_ret > -1){
			handshake_sent += handshake_send_ret;
		}else{
			error = true;
			break;
		}
	}
	// If there was an error while sending the handshake packet, move on to the next host
	if(error) return;

	// Status packet

	char status_packet[] = {1, 0x00}; // Length, packet id

	// Send status packet
	int status_len = sizeof(status_packet);
	int status_sent = send(sockfd, status_packet, status_len, 0);
	while(status_sent < status_len){
		int status_send_ret = send(sockfd, status_packet, status_len - status_sent, 0);
		if(status_send_ret > -1){
			status_sent += status_send_ret;
		}else{
			error = true;
			break;
		}
	}
	// If there was an error while sending the status packet, move on to the next host
	if(error) return;

	// Receive response

	constexpr int recv_buffer_size = 2048;
	char recv_buffer[recv_buffer_size];
	std::vector<char> message_vec;

	int bytes_read = 0;
	while(1){ // Keep reading until socket is empty (will break from inside)

		// Read one buffer-full of data

		// Poll the socket until there's something to read (or we time out)
		constexpr int num_pfds = 1;
		struct pollfd pfds[num_pfds];
		pfds[0].fd = sockfd;
		pfds[0].events = POLLIN;
		int num_events = poll(pfds, num_pfds, POLL_TIMEOUT);

		// If there was something to read, read
		if(num_events != 0) bytes_read = recv(sockfd, recv_buffer, recv_buffer_size, 0);

		// If there was an error,
		if(bytes_read == -1 || num_events == 0 || bytes_read == 0){
			if(errno == EAGAIN || errno == EWOULDBLOCK || num_events == 0 || bytes_read == 0){
				// Stop reading if EOF is reached
				break;
			}else{
				// If it was an actual error, report and go to the next host
				std::cerr << "Read failed. errno = " << errno << std::endl;
				error = true;
				break;
			}					
		}else{ // Otherwise, append this chunk to the rest
			for(int i = 0; i < bytes_read; i++){
				message_vec.push_back(recv_buffer[i]);
			}
		}
	}
	
	// If there was an error receiving, move on to the next host
	if(error) return;

	// If we received no data, move on to the next host
	if(message_vec.size() == 0){
		std::cerr << "Empty response" << std::endl;
		return;
	}


	// Parse the Minecraft stuff


	char *currentByte = message_vec.data();
	// Length of the packet
	bool ok;
	int SLP_length = parseVarInt(currentByte, message_vec.size(), ok);
	if(!ok){
		std::cerr << "Malformed packet, can't read size" << std::endl;
		return;
	}

	// Verify packet type
	if(*(currentByte++) != 0x00){
		std::cerr << "Not SLP response. Current byte is " << std::hex << *(currentByte - 1) << std::endl;
		return;
	}

	// Get SLP response contents

	// Length of the JSON payload
	int json_length = parseVarInt(currentByte, message_vec.size() - (currentByte - message_vec.data()), ok);
	if(!ok){
		std::cerr << "Malformed packet, can't read json size" << std::endl;
		return;
	}

	// The JSON itself
	// TODO: std::string.c_str() exists, use it
	char *json_buff = new char[json_length + 1];
	memcpy(json_buff, currentByte, json_length);
	json_buff[json_length] = '\0';
	std::string json_new(json_buff);
	json = json_new;
	delete[] json_buff;

	// Close the socket and mark it closed by setting the file descriptor to -1
	close(sockfd);
	sockfd = -1;
}

int main(int argc, char **argv){
	// Check argc
	if(argc != 3){
		std::cerr << "Usage: " << argv[0] << " one_host_per_line_end_with_newline.txt out_json.json" << std::endl;
		return 1;
	}

	// Read the file containing the hosts to scan
	std::cout << "Reading file...";
	std::ifstream hosts_file;
	hosts_file.open(argv[1]);
	std::string host_line;
	while(std::getline(hosts_file, host_line)){
		char *host_line_arr = new char[host_line.size() + 1];
		memcpy(host_line_arr, host_line.c_str(), host_line.size() + 1);
		hosts.push_back(host_line_arr);
	}
	std::cout << " Done" << std::endl;


	// Catch and ignore SIGPIPE so we don't crash
	signal(SIGPIPE, SIG_IGN); // I'm told this is ok to do, but seems very wrong lol
	
	updateMessage();

	// Loop over all the hosts we need to scan
	#pragma omp parallel for
	for(int i = 0; i < hosts.size(); i++){
		auto host = hosts[i];
		// Update the user display
		//updateMessage(hosts_scanned);
		//hosts_scanned++;

		int sockfd;

		std::string json;

		search(sockfd, host, json);

		// check if the socket was left open last time and close if it was
		if(sockfd != -1){
			close(sockfd);
		}
		
		#pragma omp critical
		{
			if(json.size() != 0){
				// Write the payload to the provided file
				std::ofstream file;
				file.open(argv[2], std::ios::app);
				file << "{\"ip:\": \"" << host << "\", \"slp\": " << json << "}," << std::endl;
				file.close();
			}
			//std::cout << "Wrote one" << std::endl;
			incrementHostsScanned();
			updateMessage();
		}

	}// Keep looping, go to the next host in the vector
	
	// When we're done, update the message one last time, then report that we're done
	updateMessage();
	std::cout << std::endl << "Done." << std::endl;

	return 0;
}

