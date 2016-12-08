#Secure, Fair Fantasy Sports
Bernie Birnbaum and Shawyoun Saidon

For standard application: <br>
Compile server as follows: <br>
<tt>g++ -lssl -lcrypto -std=c++11 player.cpp server.cpp -o server</tt> <br>
Compile client as follows: <br>
<tt>g++ -std=c++11 player.cpp client.cpp -o client</tt> <br>

For secure application: 
Compile secure server as follows: <br>
<tt>g++ -lssl -lcrypto -std=c++11 player.cpp ssl_utils.cpp secure_server.cpp -o secure_server</tt> <br>
Compile secure client as follows: <br>
<tt>g++ -lssl -lcrypto -std=c++11 player.cpp ssl_utils.cpp secure_client.cpp -o secure_client</tt> <br>
Compile proxy frontend as follows: <br>
<tt>g++ -lssl -lcrypto -std=c++11 player.cpp proxy_frontend.cpp -o proxy_frontend</tt> <br>
Compile proxy backend as follows: <br>
<tt>g++ -lssl -lcrypto -std=c++11 player.cpp proxy_backend.cpp -o proxy_backend</tt> <br>
Run server with:
<tt>./server (port) </tt> <br>
Run client with:
<tt>./client (host) (port)</tt>
Run secure server with:
<tt>./secure_server (port)</tt>
Run secure client with:
<tt>./secure_client (host) (port)</tt>
Run proxy frontend with:
<tt>./proxy_frontend (port) (remote_host) (remote_port)</tt>
Run proxy backend with:
<tt>./proxy_backend (port)</tt>

##Use cases for standard application:
Run the server. <br>
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

##Use cases for secure application:
Run the server and watch for SSL handshake. <br>
Observe traffic on the wire compared to normal application. <br>
Follow same steps as normal application and verify that behavior is still the same at endpoints <br>
