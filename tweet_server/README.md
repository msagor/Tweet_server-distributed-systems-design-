
INSTALLATION:

there are some installation to be done in only router vm.

first, install mysql in all the vms using the commands-
"sudo apt-get update"
"sudo apt-get install mysql-server" 
if it asks for username and password, pass "root" and "root"

Then, on terminal, do -
"sudo vi /etc/mysql/mysql.conf.d/mysqld.cnf"
it will open a sql file in vi mode..then if you go a little bit down youll see a line "bind-address = 127.0.0.1"
comment out that line with a '#' (assume you know how to use vi)
exit by saving.

Then, on the same terminal, do "mysql -uroot -p"
it will ask for password, pass "root"
then youll see mysql prompt. then copy paste the below line and press enter
"GRANT ALL PRIVILEGES ON *.* TO 'root'@'%' IDENTIFIED BY 'root';"
then you can exit out by pressing ctrl+D

Lastly, execute these below three commands.
"sudo service mysql restart"
"sudo ufw allow 3306/tcp"
"sudo service ufw restart"

Now you can exit out of the terminal

--------------------------------------------------------

how to compile:

creating the auto-generated rpc files as hw#2,

"make" -> generates the files

"make clean" -> removes the generated files

-----------------------------------------------------------

how to run:

To run the router: ./router (it will run the router in 0.0.0.0 in 3410 port which is always hardcoded)

To run the server: ./tsd -h server_host -p _server_port -r router_host &

OR, open the startup.py file, edit the server_host, server_port, router_host fields, and on terminal, do "python startup.py". THat will basically run the above command and run the server.

To run the client: ./tsc -r router_host -u username

------------------------------------------------------------



