import os

server_host = "fill_here"	#put this server's host (this vms ip)
server_port = "fill_here"	#put server's port
router_host = "fill_here"	#put router host here (the vm ip in which router is running)
#router_port			#hardcoded inside all files as 3410


os.system("./tsd -h "+ server_host + " -p "+ server_port +" -r " + router_host + "&")
