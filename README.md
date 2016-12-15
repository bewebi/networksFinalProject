#Secure, Fair Fantasy Sports
Bernie Birnbaum and Shawyoun Shaidani

<b>NOTE: Final, integrated product is in the folder labeled "unified." All filenames refer to the files in this location.</b>

<h2>Compilation</h2>
Compile server as follows:<br>
<tt>g++ -lssl -lcrypto -std=c++11 player.cpp demo_ssl_utils.cpp server.cpp -o server</tt> <br>
Compile client as follows: <br>
<tt>g++ -lssl -lcrypto -std=c++11 player.cpp demo_ssl_utils.cpp client.cpp -o client</tt> <br>
Compile proxy as follows: <br>
<tt>g++ -lssl -lcrypto -std=c++11 player.cpp demo_ssl_utils.cpp proxy_frontend.cpp -o proxy_frontend</tt> <br>

<h2>Running the application</h2>

<b> Note: in all versions, make sure the server has data file "nba1516.csv" in same directory as itself.</b>

For basic application:<br>
Run server with: <tt>./server port </tt>
Run client with: <tt>./client serverhostname serverport</tt><br>

<b>Note: in encrypted versions, make sure the server has a certificate file called "newreq.pem" in same directory as itself. One can be generated with the following command:</b> <tt>openssl req -newkey rsa:2048 -new -nodes -x509 -days 3650 -keyout newreq.pem -out newreq.pem</tt>

For encrypted application: <br>
Run encrypted server with: <tt>./server (port) -s</tt>
Run encrypted client with: <tt>./client (host) (port) -s</tt> <br> <br>

For proxy application <b>(in this order)</b>: <br>
Run server with: <tt>./server port -p</tt><br>
Run proxy with:<tt>./proxy_frontend port serverhost serverport</tt><br>
Run client with:<tt>./client proxyhost proxyport<br>

##Use and test cases for standard application
Start the server. <br>
Connect and disconnect clients at will. <br>
Follow prompt instructions to send messages, view drafted players, start the draft, draft players, etc. <br>
Some suggestions:
<ul>
	<li> Log out and log back in with correct password (or risk getting booted with the wrong password!)</li>
	<li> Quit permanently and see that you are not remembered if you return</li>
	<li> Toggle draft readiness when you are the only client, and when there are others </li>
	<li> Conduct a full draft with multiple clients </li>
	<li> Run clients with different latencies side by side, see that both have the same window for response, receive draft related messages in within the same RTT, and that the quickest reaction is rewarded </li>
	<li> Have everyone ready to draft except one client and have that client quit </li>
	<li> Log out during a draft...then log back in, see your team remembered and resume drafting</li>
	<li> Have the slowest client leave and observe increased draft speed...or have a slow client log in and see the opposite </li>
	<li> Log out and log back in with different latency </li>
	<li> Use a connection with variable latency and notice the engine adjust over time</li>
	<li> Use a debugger to verify that the passwords are encrypted </li>
	<li> Sever the connection between the client and the server to demonstrate the offline abilities of the client </li>
	<li> Send chat(s) in between draft rounds; observe next round will wait until you are done </li>
	<li> Anything else you can think of! (Hopefully we've handled it :P) </li>
</ul>

##Use and test cases for secure application:
<ul>
	<li>Start the server in secure mode (-s).</li>
	<li>Start a secure client pointing to the server. Select any of the options for TLS, then anything but "y" for prompt about trusting self-signed certificates. Observe that the handshake fails because the cert cannot be verified.</li>
	<li>Run ssldump against the interface of the client or server. Start a client again, using any of the TLS options, and this time type "y" for the prompt about trusting self-signed certs.
	<li>Observe in ssldump output that the handshake has taken place. Also note the version of TLS used (TLS 1.0 appears as SSL 3.1, 1.1 shows up as 3.2, 1.2 is 3.3) and verify it matches what you requested. Vary versions for different clients to same server.
	<li>As you begin using application, confirm that ssldump is showing this as "application data." In unencrypted mode, none of this data would show up in the output.</li>
	<li>Using packet sniffer like Wireshark, confirm that the data is scrambled. Compare to unencrypted mode, where you can observe the traffic in plaintext.</li>
	<li>In the logout (not permanent) case, verify in ssldump that a new handshake is done when the client returns. </li>
	<li>Try connecting to secure client with insecure client, and secure client with insecure server. Verify that the connection eventually fails.
	<li>Follow same steps as above ("Use cases for normal application") and verify that behavior is still the same at endpoints. </li>
</ul>

##Use and test cases for proxy application:
<ul>
	<li>Run the server in proxy mode (-p), then have proxy_frontend connect to it. Run the client and connect it to the proxy frontend. </li>
	<li>Watch traffic on the wire (Wireshark or tcpdump) and verify that it is all going through proxy_frontend instead of directly between client and server.</li>
	<li>Run sockets tool "ss -t" to see number of sockets opened up on each host/port combination. Verify that each client has a pair of sockets (read / write) against the proxy_frontend, but there's only pair between the server and proxy_frontend.</li>
	<li>After connection has been established between proxy_frontend and server, try to connect directly to the server as a client. Verify that the connection is killed (it briefly accepts, then closes immediately). </li>
	<li>Disconnect from proxy_frontend (CTRL-C or logout) and verify that you have to be authenticated afterwards (server prompts you for password and says "welcome back" with your data if correct.) The server was made aware that you disconnected from the proxy.</li>
	<li>Follow same steps as above ("Use cases for normal application") and verify that behavior is as expected.</li>
</ul>
