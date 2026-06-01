/***********************************************************************
Tcping.exe -- A tcp probe utility
Copyright (C) 2005-2017 Eli Fulkerson

This program is free software; you can redistribute it and/or
modify it under the terms of the GNU General Public License
as published by the Free Software Foundation; either version 2
of the License, or (at your option) any later version.

This program is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.

You should have received a copy of the GNU General Public License
along with this program; if not, write to the Free Software
Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301, USA.

----------------------------------------------------------------------

Other license terms may be negotiable.  Contact the author if you would
like a copy that is licensed differently.

Contact information (as well as this program) lives at http://www.elifulkerson.com

----------------------------------------------------------------------

This application includes public domain code from the Winsock Programmer's FAQ:
  http://www.tangentsoft.net/wskfaq/
... and a big thank you to the maintainers and contributors therein.

***********************************************************************/

#define _WINSOCK_DEPRECATED_NO_WARNINGS

#include "net_compat.h"
#include <iostream>
#include <string.h>
#include <time.h>
#include <vector>
#include <fstream>
#include <string>
#include <sstream>

#include "tee.h"
#include "ws-util.h"
#include "tcping.h"
#include "base64.h"

#ifdef _MSC_VER
#pragma comment(lib, "Ws2_32.lib")
#endif

#define NUM_PROBES 4

using namespace std;


// Prototypes

SOCKET EstablishConnection(ADDRINFO *address, int ping_timeout, int force_send_byte, ADDRINFO *src_address, int &errcode, bool blocking);
SOCKET HTTP_EstablishConnection(ADDRINFO *address, ADDRINFO *src_address);

void formatIP(std::string &abuffer, ADDRINFO *address);


int CTRL_C_ABORT;

const int BufferSize = 1024;


bool SendHttp(SOCKET sd, char* server, const char* document, int http_cmd, int using_proxy, int using_credentials, char* hashed_credentials) {
    char message[1024];
    char cmd[5];

    switch (http_cmd) {
    case HTTP_GET:
		strcpy_s(cmd, sizeof(cmd), "GET");
        break;
    case HTTP_HEAD:
		strcpy_s(cmd, sizeof(cmd), "HEAD");
        break;
    case HTTP_POST:
		strcpy_s(cmd, sizeof(cmd), "POST");
        break;
    }

    if (document == NULL) {
        document = "/";
    }

	if (using_credentials == 0) {
		if (using_proxy == 0) {
			sprintf_s(message, sizeof(message), "%s /%s HTTP/1.1\r\nHost: %s\r\nConnection: close\r\nUser-Agent: tcping.exe (www.elifulkerson.com)\r\n\r\n", cmd, document, server);
		}
		else {
			sprintf_s(message, sizeof(message), "%s http://%s/%s HTTP/1.1\r\nConnection: close\r\nUser-Agent: tcping.exe (www.elifulkerson.com)\r\n\r\n", cmd, server, document);
		}
	}
	else {
		if (using_proxy == 0) {
			sprintf_s(message, sizeof(message), "%s /%s HTTP/1.1\r\nHost: %s\r\nConnection: close\r\nUser-Agent: tcping.exe (www.elifulkerson.com)\r\nProxy-Authorization: Basic %s\r\n\r\n", cmd, document, server, hashed_credentials);
		}
		else {
			sprintf_s(message, sizeof(message), "%s http://%s/%s HTTP/1.1\r\nConnection: close\r\nUser-Agent: tcping.exe (www.elifulkerson.com)\r\nProxy-Authorization: Basic %s\r\n\r\n", cmd, server, document, hashed_credentials);
		}
	}

    const int messageLen = (int)strlen(message);
	
    // Send the string to the server
    if (send(sd, message, messageLen, 0) != SOCKET_ERROR) {
        return true;
    } else {
        return false;
    }
}


//// ReadReply /////////////////////////////////////////////////////////
// Read the reply packet and check it for sanity.  Returns -1 on
// error, 0 on connection closed, > 0 on success.

int ReadReply(SOCKET sd, int &bytes_received, int &http_status) {
    // Read reply from server
    char acReadBuffer[BufferSize];
    char acTrashBuffer[BufferSize];

    int nTotalBytes = 0;
    int nNewBytes = 0;
    while (1) {

        if (nTotalBytes < BufferSize) {
            nNewBytes = recv(sd, acReadBuffer + nTotalBytes, BufferSize - nTotalBytes, 0);
        } else {
            nNewBytes = recv(sd, acTrashBuffer, BufferSize, 0);
        }

        if (nNewBytes == SOCKET_ERROR) {
            return -1;
        } else if (nNewBytes == 0) {
            break;
        }

        nTotalBytes += nNewBytes;
    }

    bytes_received =  nTotalBytes;

    // NUL-terminate before treating the reply as a C string.  recv() does not,
    // and nTotalBytes can equal BufferSize, so clamp to the last byte.
    int term = (nTotalBytes < BufferSize) ? nTotalBytes : (BufferSize - 1);
    acReadBuffer[term] = '\0';

    http_status = 0;

    // parse out the http status from the first line, e.g. "HTTP/1.1 200 OK".
    // Guard every step: a non-HTTP or empty reply has no spaces and used to
    // dereference a NULL pointer here.
    char* statusptr = strchr(acReadBuffer, ' ');
    if (statusptr != NULL) {
        ++statusptr;
        char* tmpptr = strchr(statusptr, ' ');
        if (tmpptr != NULL) {
            *tmpptr = '\0';
        }
        http_status = atoi(statusptr);
    }
    return 0;
}

void request_abort() {
    if (CTRL_C_ABORT == 1) {
        cout.flush();
        cout << "Wow, you really mean it.  I'm sorry... I'll stop now. :(" << endl;
        exit(1);
    }
    cout << "Control-C" << endl;
    CTRL_C_ABORT = 1;
}

#ifdef _WIN32
BOOL WINAPI console_ctrl_handler(DWORD /*ctrlType*/) {
    request_abort();
    return TRUE;
}
#else
void sigint_handler(int /*signum*/) {
    request_abort();
}
#endif

// Install the platform's Ctrl-C handler.  On POSIX we also ignore SIGPIPE so a
// peer that closes mid-send can't kill the process.
void install_signal_handlers() {
#ifdef _WIN32
    SetConsoleCtrlHandler(console_ctrl_handler, TRUE);
#else
    signal(SIGINT, sigint_handler);
    signal(SIGPIPE, SIG_IGN);
#endif
}

void COLOR_RESET(int use_color) {
	if (use_color == 1) {
		printf("%c[%dm", 0x1B, 0);
	}

#ifdef _WIN32
	if (use_color == 2) {
		HANDLE hConsole = GetStdHandle(STD_OUTPUT_HANDLE);
		SetConsoleTextAttribute(hConsole, 7);
	}
#endif
}

void COLOR_RED(int use_color) {
	if (use_color == 1) {
		printf("%c[%dm", 0x1B, 31);
	}

#ifdef _WIN32
	if (use_color == 2) {
		HANDLE hConsole = GetStdHandle(STD_OUTPUT_HANDLE);
		SetConsoleTextAttribute(hConsole, 12);
	}
#endif
}


int DoWinsock(char* pcHost, int nPort, int times_to_ping, double ping_interval, int include_timestamp, int beep_mode, int ping_timeout, int relookup_interval, int auto_exit_on_success, int force_send_byte, int include_url, int use_http, char* docptr, int http_cmd, int include_jitter, int jitter_sample_size, char* logfile, int use_logfile, int ipv, char* proxy_server, int proxy_port, int using_credentials, char* proxy_credentials, int only_changes, int no_statistics, int giveup_count, Tee &out, int use_source_address, char *src_address, bool blocking, int always_print_domain, int use_color) {

	COLOR_RESET(use_color);

	// Work on our own copy of the hostname.  This avoids writing back into the
	// caller's argv and means the proxy overwrite below can't overflow (pcHost
	// originally pointed at an argv string of unknown, possibly smaller, size).
	char host_storage[2048];
	sprintf_s(host_storage, sizeof(host_storage), "%s", pcHost);
	pcHost = host_storage;

	char web_server[2048];
	int using_proxy = 0;
	// if we are using a http proxy server, the pcHost needs to be the server address
	if (proxy_server[0] == 0) {
		sprintf_s(web_server, sizeof(web_server), "%s", pcHost);
		//web_server[0] = 0;
	} else {
		sprintf_s(web_server, sizeof(web_server), "%s", pcHost);
		sprintf_s(pcHost, sizeof(host_storage), "%s", proxy_server);
		nPort = proxy_port;
		using_proxy = 1;
	}

	char hashed_credentials[2048];
	if (using_credentials == 1) {
		const std::string s(proxy_credentials);
		std::string encoded = base64_encode(reinterpret_cast<const unsigned char*>(s.c_str()), s.length());
		sprintf_s(hashed_credentials, sizeof(hashed_credentials), "%s", encoded.c_str());
	}


    install_signal_handlers();
    CTRL_C_ABORT = 0;

    int bytes_received = 0;
    int http_status = 0;

    double bps = 0;

    int number_of_pings = 0;	// total number of tcpings issued
    double running_total_ms = 0;	// running total of values of pings... divide by number_of_pings for avg
    double lowest_ping = 50000;		// lowest ping in series ... starts high so it will drop
    double max_ping = 0;			// highest ping in series

    double running_total_ms_http = 0;
    double lowest_ping_http = 50000;
    double max_ping_http = 0;

    int success_counter = 0;
    int failure_counter = 0;
    int deferred_counter = 0;

	int sequential_failure_counter = 0;

    int loopcounter = 0;			// number of probes to send

    // Timestamp variablees
	struct tm  timeinfo;
	char dateStr[11];
	char timeStr[9];

    int beep_flag = -1;  // 0 for we're down, 1 for we're up
    double response_time = 0;
    double http_response_time = 0;

    int have_valid_target = 1;

    // jitter rolling average
    vector<double> jitterbuffer(jitter_sample_size);
    vector<double> http_jitterbuffer(jitter_sample_size);

    int jitterpos = 0;
    int success_flag = 0;   // For jitter rolling average we have to remember if we were successful *this* cycle, not just accumulating things
    double j;

	// I'm deliberately using abs() for jitter because negative numbers screw with max/min
	double current_jitter = 0;
	double running_total_abs_jitter = 0;
	double lowest_abs_jitter = 50000;
	double max_abs_jitter = 0;
	
	double current_http_jitter = 0;
	double running_total_abs_http_jitter = 0;
	double lowest_abs_http_jitter = 50000;
	double max_abs_http_jitter = 0;




	bool last_cycle_success = false;
	int number_same_cycles = 0;

	ADDRINFO hint, *AddrInfo = NULL, *AI;
	char p[6];
	int r;
	int found;

	sprintf_s(p, sizeof(p), "%d", nPort);
    memset(&hint, 0, sizeof (hint));
    hint.ai_family = PF_UNSPEC;
    hint.ai_socktype = SOCK_STREAM;



	
	
	
	// Find the server's address	
	r = getaddrinfo(pcHost, p, &hint, &AddrInfo);
    
	if (r != 0 ) {
		
		if (relookup_interval == -1) {
			COLOR_RED(use_color);
			out.pf("DNS: Could not find host - %s, aborting\n", pcHost);
			COLOR_RESET(use_color);
			return 3;
		}
		else {
			have_valid_target = 0;
		}
	}
	found = 0;
	for (AI = AddrInfo; AI != NULL; AI = AI->ai_next) {
		if ((AI->ai_family == AF_UNSPEC && ipv == 0) ||
			(AI->ai_family == AF_INET && ipv != 6) ||
			(AI->ai_family == AF_INET6 && ipv != 4)) {
			found = 1;
			break;
		}
	}
	if (found == 0) {
		if (relookup_interval == -1) {
			COLOR_RED(use_color);
			out.pf("DNS: No valid host found in AddrInfo for that type\n");
			COLOR_RESET(use_color);
			return 3;
		}
		else {
			have_valid_target = 0;
		}
	}
	
	// source IP
	ADDRINFO *SRCAI = NULL;

	if (use_source_address != 0) {
		r = getaddrinfo(src_address, NULL, NULL, &SRCAI);
		
		if (r != 0) {
			COLOR_RED(use_color);
			out.pf("-S:  You specified '%s' as a source address, couldn't do anything with that, aborting.\n", src_address);
			COLOR_RESET(use_color);
			return 4;
		}
		
		SOCKET sd = socket(AI->ai_family, AI->ai_socktype, AI->ai_protocol);
		
		r = ::bind(sd, SRCAI->ai_addr, (int)SRCAI->ai_addrlen);

		if (r == SOCKET_ERROR) {
			COLOR_RED(use_color);
			out.pf("Binding local source address '%s' failed with error %u\n", src_address, WSAGetLastError());
			COLOR_RESET(use_color);
			return 5;
		}


	}

	int errorcode_stash = 0;

    while ((loopcounter < times_to_ping || times_to_ping == -1) && CTRL_C_ABORT == 0 ) {

        success_flag = 0;

        if (((number_of_pings % relookup_interval == 0) && (relookup_interval != -1) && number_of_pings > 0) || have_valid_target == 0) {
			// Free the previous cycle's results before we overwrite the
			// pointer, otherwise every relookup leaks an addrinfo list.
			if (AddrInfo != NULL) {
				freeaddrinfo(AddrInfo);
				AddrInfo = NULL;
			}
			// Find the server's address
			// Duplicate code here because dealing with resource leaks, getaddrinfo and the
			// differing IPV4 vs IPV6 structures was just obnoxious.
			found = 0;
			r = getaddrinfo(pcHost, p, &hint, &AddrInfo);

			if (r != 0) {
				out.pf("DNS: Could not find host - %s\n", pcHost);
				have_valid_target = 0;
			}
			for (AI = AddrInfo; AI !=NULL; AI=AI->ai_next) {
				if ( (AI->ai_family == AF_UNSPEC && ipv == 0) ||
				     (AI->ai_family == AF_INET && ipv != 6) ||
				     (AI->ai_family == AF_INET6 && ipv != 4) ) {
				
					have_valid_target = 1;
					found = 1;
					std::string abuffer;
					formatIP(abuffer, AI);
					out.pf("DNS: %s is %s\n", pcHost, abuffer.c_str());
					break;
				}
				if (found == 0) {
					out.pf("DNS: No valid host found in AddrInfo for that type\n");
				}	 
			}
        }

        if (include_timestamp == 1) {
			local_time_now(&timeinfo);
			strftime(dateStr, sizeof(dateStr), "%Y:%m:%d", &timeinfo);
			strftime(timeStr, sizeof(timeStr), "%H:%M:%S", &timeinfo);
			out.pf("%s %s ", dateStr, timeStr);
        }

        if (have_valid_target == 1) {

            SOCKET sd;

            // start the timer right before we do the connection
            double response_timer1 = now_ms();

            // Connect to the server
            if (use_http == 0) {
                sd = EstablishConnection(AI, ping_timeout, force_send_byte, SRCAI, errorcode_stash, blocking);
            } else {
                sd = HTTP_EstablishConnection(AI, SRCAI);
            }

            // grab the time as soon as possible after we return
            double response_timer2 = now_ms();

            if (sd == INVALID_SOCKET) {
			
				if (only_changes == 1) {
					if (last_cycle_success == false) 
					{
						// no change, so kill the output
						out.enable(false);
						number_same_cycles += 1;
					} else {
						out.enable(true);
						if (number_of_pings > 0) {
							out.pf("(%d successful)\n", number_same_cycles);
						}
						number_same_cycles = 0;
					}
					last_cycle_success = false;
				}
				std::string abuffer;
				formatIP(abuffer, AI);
				
				COLOR_RED(use_color);
				if (always_print_domain == 0) {
					out.pf("Probing %s:%d/tcp - ", abuffer.c_str(), nPort);
				}
				else {
					out.pf("Probing %s:%d/tcp - ", pcHost, nPort);
				}

				//if (WSAGetLastError() != 0) {
				if (errorcode_stash != 0) {
					out.p( WSAGetLastErrorMessage("",errorcode_stash));
					
				} else {
					out.p( "No response" );
				}
                failure_counter++;
				sequential_failure_counter++;

                if (beep_mode == 4 || beep_mode == 1 || (beep_mode == 3 && beep_flag == 1)) {
                    cout << " " << char(7) << "*" << char(7) << "*";
                }
                beep_flag = 0;
            } else {
			
				if (only_changes == 1) {
					if (last_cycle_success == true) 
					{
						// no change, so kill the output
						
						out.enable(false);
						number_same_cycles += 1;
					} else {
						out.enable(true);
						if (number_of_pings > 0) {
							out.pf("(%d unsuccessful)\n", number_same_cycles);
						}

						number_same_cycles = 0;
					}
					last_cycle_success = true;
					sequential_failure_counter = 0;
				}
				
				std::string abuffer;
				formatIP(abuffer, AI);

				if (always_print_domain == 0) {	
					out.pf("Probing %s:%d/tcp - ", abuffer.c_str(), nPort);
				}
				else {
					out.pf("Probing %s:%d/tcp - ", pcHost, nPort);
				}

                if (use_http == 0) {
                    out.p("Port is open");
                    success_counter++;
                    success_flag = 1;
                } else {
                    // consider only incrementing if http response @@
                    out.p("HTTP is open");
                    success_counter++;
                    success_flag = 1;

                    // send http send/response
                    double http_timer1 = now_ms();

                    SendHttp(sd, web_server, docptr, http_cmd, using_proxy, using_credentials, hashed_credentials);
                    ReadReply(sd, bytes_received, http_status);
                    double http_timer2 = now_ms();
                    http_response_time = http_timer2 - http_timer1;
					closesocket(sd);
                }

                if (beep_mode == 4 || beep_mode == 2 || (beep_mode == 3 && beep_flag == 0)) {
                    cout << " *" << char(7);
                }
                beep_flag = 1;
            }
            // Shut connection down
            if (ShutdownConnection(sd)) {
                // room here for connection shutdown success check...
            } else {
                // room here for connection shutdown failure check...
            }

            response_time = response_timer2 - response_timer1;

            out.pf( " - time=%0.3fms ", response_time);

            if (use_http == 1) {
                if (include_url == 1) {
                    if (docptr != NULL) {
                        out.pf( "page:http://%s/%s ",pcHost,docptr);
                    } else {
                        out.pf( "page:http://%s ", pcHost);
                    }
                }

                out.pf("rcv_time=%0.3f status=%d bytes=%d ", http_response_time, http_status, bytes_received);

                bps = bytes_received * 1000 / http_response_time;
                bps = bps * 8 / 1000;

                out.pf("kbit/s=~%0.3f ",bps);
            }


            // Calculate the statistics...
            number_of_pings++;

            if (sd != INVALID_SOCKET) {
                running_total_ms += response_time;

                if (response_time < lowest_ping) {
                    lowest_ping = response_time;
                }

                if (response_time > max_ping) {
                    max_ping = response_time;
                }

                if (use_http == 1) {

                    running_total_ms_http += http_response_time;

                    if (http_response_time < lowest_ping_http) {
                        lowest_ping_http = http_response_time;
                    }

                    if (http_response_time > max_ping_http) {
                        max_ping_http = http_response_time;
                    }
                }
            }

            /*
              Two ways to measure jitter.  If jitter_sample_size == 0, then its a total/times, non inclusive of the current go.
              Otherwise, we calculate it based on the prior [jitter_sample_size] values, non inclusive.
             */

            if (include_jitter == 1 && success_counter > 1) {
                if (jitter_sample_size == 0) {
                    // we didn't specify a sample size, so no rolling average

					current_jitter = response_time - ((running_total_ms - response_time) / (success_counter - 1));

					//out.pf("jitter=%0.3f ", response_time - ((running_total_ms - response_time) / (success_counter - 1)));
					out.pf("jitter=%0.3f ", current_jitter);

					if (max_abs_jitter < abs(current_jitter)) {
						max_abs_jitter = abs(current_jitter);
					}

					if (lowest_abs_jitter > abs(current_jitter)) {
						lowest_abs_jitter = abs(current_jitter);
					}

					running_total_abs_jitter += abs(current_jitter);

                    if (use_http == 1) {

						current_http_jitter = http_response_time - ((running_total_ms_http - http_response_time) / (success_counter - 1));
						//out.pf("rcv_jitter=%0.3f ", http_response_time - ((running_total_ms_http - http_response_time) / (success_counter - 1)));
						out.pf("rcv_jitter=%0.3f ", current_http_jitter);

						if (max_abs_http_jitter < abs(current_http_jitter)) {
							max_abs_http_jitter = abs(current_http_jitter);
						}

						if (lowest_abs_http_jitter > abs(current_http_jitter)) {
							lowest_abs_http_jitter = abs(current_http_jitter);
						}

						running_total_abs_http_jitter += abs(current_http_jitter);
                    }

                } else {

                    j = 0;

                    for (int x=0; x< min(jitter_sample_size, success_counter - 1); x++) {

                        j = j + jitterbuffer[x];

                    }
					current_jitter = response_time - (j / min(success_counter - 1, jitter_sample_size));
					//out.pf("jitter=%0.3f ", response_time - (j / min(success_counter - 1, jitter_sample_size)));
					out.pf("jitter=%0.3f ", current_jitter);

					if (max_abs_jitter < abs(current_jitter)) {
						max_abs_jitter = abs(current_jitter);
					}

					if (lowest_abs_jitter > abs(current_jitter)) {
						lowest_abs_jitter = abs(current_jitter);
					}

					running_total_abs_jitter += abs(current_jitter);

                    if (use_http == 1) {
                        j = 0;
                        for (int x=0; x< min(jitter_sample_size, success_counter - 1); x++) {
                            j = j + http_jitterbuffer[x];
                        }
						current_http_jitter = http_response_time - (j / min(success_counter - 1, jitter_sample_size));
						//out.pf("rcv_jitter=%0.3f ", http_response_time - (j / min(success_counter - 1, jitter_sample_size)));
						out.pf("rcv_jitter=%0.3f ", current_http_jitter);

						if (max_abs_http_jitter < abs(current_http_jitter)) {
							max_abs_http_jitter = abs(current_http_jitter);
						}

						if (lowest_abs_http_jitter > abs(current_http_jitter)) {
							lowest_abs_http_jitter = abs(current_http_jitter);
						}

						running_total_abs_http_jitter += abs(current_http_jitter);
                    }
                }
            }

            if (success_flag == 1 && jitter_sample_size > 0) {
                jitterbuffer[jitterpos] = response_time;
                http_jitterbuffer[jitterpos] = http_response_time;
                jitterpos++;

                // simple rolling average - go back to the beginning of the array once we fill it
                if (jitterpos == jitter_sample_size) {
                    jitterpos = 0;
                }
            }


			COLOR_RESET(use_color);
            out.p("\n");


            loopcounter++;
            if ((loopcounter == times_to_ping) || ((auto_exit_on_success == 1) && (success_counter > 0))) {
                break;
            }

			if (sequential_failure_counter >= giveup_count && giveup_count != 0) {
				break;
			}

        } else {
            // no valid target
            response_time = 0;
            deferred_counter++;
            out.p("No host to ping.\n");
        }

        int zzz = 0;
        double wakeup = (ping_interval * 1000) - response_time;
        if (wakeup > 0 ) {
            while (zzz < wakeup && CTRL_C_ABORT ==0) {
                sleep_ms(10);
                zzz += 10;
            }
        }
    }

	out.enable(true);

	if (!no_statistics) {

		std::string abuffer;
		if (have_valid_target == 1) {
			formatIP(abuffer, AI);
		}
		else {
			// if we have a bouncing DNS host, we don't have an IP to format correctly, so just spit out what they gave us as an argument...
			abuffer = pcHost;
		}
		
		out.pf("\nPing statistics for %s:%d\n", abuffer.c_str(), nPort);
		out.pf("     %d probes sent. \n", number_of_pings);

		float fail_percent = 100 * (float)failure_counter / ((float)success_counter + (float)failure_counter);

		// Single %% here: Tee::pf formats once with vsnprintf, then Tee::p prints
		// with a literal "%s", so the text is no longer run through printf twice.
		out.pf("     %d successful, %d failed.  (%0.2f%% fail)\n", success_counter, failure_counter, fail_percent);
		
		if (deferred_counter > 0) {

			out.pf("     %d skipped due to failed DNS lookup.\n", deferred_counter);
		}
		if (success_counter > 0) {
			if (failure_counter > 0) {
				out.p("Approximate trip times in milli-seconds (successful connections only):\n");
			}
			else {
				out.p("Approximate trip times in milli-seconds:\n");
			}

			out.pf("     Minimum = %0.3fms, Maximum = %0.3fms, Average = %0.3fms\n", lowest_ping, max_ping, running_total_ms / success_counter);

			if (use_http == 1) {
				out.p("Approximate download times in milli-seconds:\n");
				out.pf("     Minimum = %0.3fms, Maximum = %0.3fms, Average = %0.3fms\n", lowest_ping_http, max_ping_http, running_total_ms_http / success_counter);
			}

			if (include_jitter && success_counter > 1) {
				out.p("Jitter:\n");
				out.pf("     Minimum = %0.3fms, Maximum = %0.3fms, Average = %0.3fms\n", lowest_abs_jitter, max_abs_jitter, running_total_abs_jitter / (success_counter - 1));
				if (use_http) {
					out.p("HTTP response jitter:\n");
					out.pf("     Minimum = %0.3fms, Maximum = %0.3fms, Average = %0.3fms\n", lowest_abs_http_jitter, max_abs_http_jitter, running_total_abs_http_jitter / (success_counter - 1));
				}
			}
		}
		else {
			out.p("Was unable to connect, cannot provide trip statistics.\n");
		}
	}
	if (AddrInfo != NULL) {
		freeaddrinfo(AddrInfo);
	}

    // report our total, abject failure.
    if (success_counter == 0) {
        return 1;
    }

    // return our intermittent failure
    if (success_counter > 0 && failure_counter > 0) {
        return 2;
    }

    return 0;
}

//// EstablishConnection ///////////////////////////////////////////////
// Connects to a given address, on a given port, both of which must be
// in network byte order.  Returns newly-connected socket if we succeed,
// or INVALID_SOCKET if we fail.

SOCKET EstablishConnection(ADDRINFO* address, int ping_timeout, int force_send_byte, ADDRINFO* src_address, int &errorcode, bool blocking) {

    errorcode = 0;

    // Create a stream socket
    SOCKET sd = socket(address->ai_family, address->ai_socktype, address->ai_protocol);
    if (sd == INVALID_SOCKET) {
        errorcode = WSAGetLastError();
        return INVALID_SOCKET;
    }

    if (src_address != NULL) {
        ::bind(sd, src_address->ai_addr, (int)src_address->ai_addrlen);
    }

    // In blocking mode connect() blocks (so -w can't bound it, but it can tell a
    // refused connection apart from a timeout).  Otherwise we do a non-blocking
    // connect and wait for completion with select(), enforcing the -w timeout.
    if (blocking) {
        if (connect(sd, address->ai_addr, (int)address->ai_addrlen) == SOCKET_ERROR) {
            errorcode = WSAGetLastError();
            closesocket(sd);
            return INVALID_SOCKET;
        }
    } else {
        set_nonblocking(sd, true);

        if (connect(sd, address->ai_addr, (int)address->ai_addrlen) == SOCKET_ERROR) {
            int err = WSAGetLastError();
            if (!connect_in_progress(err)) {
                // Failed outright (e.g. connection refused).
                errorcode = err;
                closesocket(sd);
                return INVALID_SOCKET;
            }

            // Wait for the connect to complete, in short slices so Ctrl-C stays
            // responsive and we can honor the overall -w timeout.
            double start = now_ms();
            bool connected = false;
            while (!CTRL_C_ABORT) {
                double remaining = ping_timeout - (now_ms() - start);
                if (remaining <= 0) {
                    errorcode = WSAETIMEDOUT;
                    break;
                }

                int slice = remaining < 100 ? (int)remaining : 100;
                struct timeval tv;
                tv.tv_sec = slice / 1000;
                tv.tv_usec = (slice % 1000) * 1000;

                fd_set writefds;
                fd_set exceptfds;
                FD_ZERO(&writefds);
                FD_ZERO(&exceptfds);
                FD_SET(sd, &writefds);
                FD_SET(sd, &exceptfds);

                int n = select((int)sd + 1, NULL, &writefds, &exceptfds, &tv);
                if (n == SOCKET_ERROR) {
                    errorcode = WSAGetLastError();
                    break;
                }
                if (n == 0) {
                    continue;  // slice expired; re-check Ctrl-C and total timeout
                }

                // Socket signaled - did the connect succeed?
                int so_error = 0;
                socklen_t len = sizeof(so_error);
                getsockopt(sd, SOL_SOCKET, SO_ERROR, (char*)&so_error, &len);
                if (so_error != 0) {
                    errorcode = so_error;
                } else {
                    connected = true;
                }
                break;
            }

            if (!connected) {
                closesocket(sd);
                return INVALID_SOCKET;
            }
        }
    }

    // Connected.  Optionally push a byte across the connection as well.
    if (force_send_byte) {
        send(sd, ".", 1, 0);
    }

    closesocket(sd);
    return sd;
}

SOCKET HTTP_EstablishConnection(ADDRINFO* address, ADDRINFO* src_address) {

    // Create a stream socket

	SOCKET sd = socket(address->ai_family, address->ai_socktype, address->ai_protocol);
    // would be nice to limit the huge timeout in cases where the tcp connection times out (as opposed
    // to bouncing off a closed port, but this stuff doesn't work for some reason.
    /*int timeout = 10;
    int tosize = sizeof(timeout);
    setsockopt(sd, SOL_SOCKET, SO_SNDTIMEO , (char*)&timeout, (int)&tosize);
    setsockopt(sd, SOL_SOCKET, SO_RCVTIMEO , (char*)&timeout, (int)&tosize);
    */

	if (src_address != NULL) {
		::bind(sd, src_address->ai_addr, (int)src_address->ai_addrlen);
	}

    if (sd != INVALID_SOCKET) {

        if (connect(sd, address->ai_addr, (int)address->ai_addrlen) == SOCKET_ERROR) {
            sd = INVALID_SOCKET;
        }
    }

    return sd;
}


void formatIP(std::string &abuffer, ADDRINFO* address) {

	char buffer[46] = {0};  // enough for the longest IPv6 text form

	// inet_ntop is available on every platform we target now (POSIX and
	// Windows Vista+), so the old inet_ntoa / getnameinfo split is gone.
	switch (address->ai_family) {
	case AF_INET: {
		struct sockaddr_in* sockaddr_ipv4 = (struct sockaddr_in*) address->ai_addr;
		inet_ntop(AF_INET, &sockaddr_ipv4->sin_addr, buffer, sizeof(buffer));
		break;
	}
	case AF_INET6: {
		struct sockaddr_in6* sockaddr_ipv6 = (struct sockaddr_in6*) address->ai_addr;
		inet_ntop(AF_INET6, &sockaddr_ipv6->sin6_addr, buffer, sizeof(buffer));
		break;
	}
	}

	abuffer = buffer;

}


int DoWinsock_Single(char* pcHost, int nPort, int times_to_ping, double ping_interval, int include_timestamp, int beep_mode, int ping_timeout, int relookup_interval, int auto_exit_on_success, int force_send_byte, int include_url, int use_http, char* docptr, int http_cmd, int include_jitter, int jitter_sample_size, char* logfile, int use_logfile, int ipv, char* proxy_server, int proxy_port, int using_credentials, char* proxy_credentials, int only_changes, int no_statistics, int giveup_count, Tee &out, int use_source_address, char *src_address, bool blocking, int always_print_domain, int use_color) {
	
	int retval;
	
	retval = DoWinsock(pcHost, nPort, times_to_ping, ping_interval, include_timestamp, beep_mode, ping_timeout, relookup_interval, auto_exit_on_success, force_send_byte, include_url, use_http, docptr, http_cmd, include_jitter, jitter_sample_size, logfile, use_logfile, ipv, proxy_server, proxy_port, using_credentials, proxy_credentials, only_changes, no_statistics, giveup_count, out, use_source_address, src_address, blocking, always_print_domain, use_color);
	return retval;
}

int DoWinsock_Multi(char* pcHost, int nPort, int times_to_ping, double ping_interval, int include_timestamp, int beep_mode, int ping_timeout, int relookup_interval, int auto_exit_on_success, int force_send_byte, int include_url, int use_http, char* docptr, int http_cmd, int include_jitter, int jitter_sample_size, char* logfile, int use_logfile, int ipv, char* proxy_server, int proxy_port, int using_credentials, char* proxy_credentials, int only_changes, int no_statistics, int giveup_count, int file_times_to_loop, char* urlfile, Tee &out, int use_source_address, char *src_address, bool blocking, int always_print_domain, int use_color) {
	
	int retval = 0;  // result if the file is empty / no lines processed


	int count = 0;
	while (count < file_times_to_loop || file_times_to_loop == -1) {
		std::ifstream input(urlfile);
		std::string line;

		//while (std::getline(input, line))
		while (std::getline(input, line))
		{
			std::stringstream ss(line);
			std::string line_ip;
			int line_port;

			if (ss >> line_ip) {
				if (ss >> line_port) {
					//out.p("success");
				}
				else {
					line_port = nPort;
				}
			}
			else {
				break;
			}

			char pcHost_f[255];
			//strcpy_s(pcHost_f, sizeof(pcHost_f), line.c_str());
			strcpy_s(pcHost_f, sizeof(pcHost_f), line_ip.c_str());
			
			nPort = line_port;

			retval = DoWinsock(pcHost_f, nPort, times_to_ping, ping_interval, include_timestamp, beep_mode, ping_timeout, relookup_interval, auto_exit_on_success, force_send_byte, include_url, use_http, docptr, http_cmd, include_jitter, jitter_sample_size, logfile, use_logfile, ipv, proxy_server, proxy_port, using_credentials, proxy_credentials, only_changes, no_statistics, giveup_count, out, use_source_address, src_address, blocking, always_print_domain, use_color);
			
			int zzz = 0;
			double wakeup = (ping_interval * 1000);
			if (wakeup > 0) {
				while (zzz < wakeup && CTRL_C_ABORT == 0) {
					sleep_ms(10);
					zzz += 10;
				}
				if (CTRL_C_ABORT == 1) {  // need to be explicit here since breaking the ping loop on an individual host doesn't return in multi mode
					return retval;
				}
			}
		}
		count++;
	}
		return retval;
}
