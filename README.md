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

<b>Note: in encrypted versions, make sure the server has a certificate file called "newreq.pem" in same directory as itself. One can be generated with the following command: openssl req -newkey rsa:2048 -new -nodes -x509 -days 3650 -keyout newreq.pem -out newreq.pem</b>

For encrypted application: <br>
Run encrypted server with: <tt>./server (port) -s</tt>
Run encrypted client with: <tt>./client (host) (port) -s</tt> <br> <br>

For proxy application <b>(in this order)</b>: <br>
Run server with: <tt>./server port -p</tt><br>
Run proxy with:<tt>./proxy_frontend port serverhost serverport</tt><br>
Run client with:<tt>./client proxyhost proxyport<br>

<h1>Use cases for standard application</h1>
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
<ul>
	<li>Run the server and watch for SSL handshake. </li>
	<li>Observe traffic on the wire compared to normal application. </li>
	<li>Follow same steps as normal application and verify that behavior is still the same at endpoints </li>
</ul>

##Use cases for proxy application:
<ul>
	<li> Run the proxy_backend, followed by the proxy frontend connecting to it.</li>
	<li>Run the client and connect to the proxy frontend.</li>
	<li>Make basic requests like client list and player list. Verify they work as expected. </li>
	<li>Do a chat and verify it gets to the proper client </li>
	<li>Do a draft request and verify the data has been updated properly </li>
	<li>Do an exit and verify the client has been removed </li>
	<li>Using debugger, determine that only one socket is being used on proxy backend. </li>
	<li>Watch traffic on the wire and verify that it is all coming from proxy_frontend instead of client or server endpoints.</li>
</ul>
