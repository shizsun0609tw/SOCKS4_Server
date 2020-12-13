#include "server.h"

boost::asio::io_context *Session::io_context_;

Session::Session(tcp::socket socket) : socket_(std::move(socket))
{
	status_str = "HTTP/1.1 200 OK\n";
}

void Session::SetContext(boost::asio::io_context *io_context)
{
	io_context_ = io_context;
}

void Session::Start()
{
	DoRead();
}

void Session::SetEnv()
{
	#ifdef __linux__
	setenv("REQUEST_METHOD", REQUEST_METHOD.c_str(), 1);
	setenv("REQUEST_URI", REQUEST_URI.c_str(), 1);
	setenv("QUERY_STRING", QUERY_STRING.c_str(), 1);
	setenv("SERVER_PROTOCOL", SERVER_PROTOCOL.c_str(), 1);
	setenv("HTTP_HOST", HTTP_HOST.c_str(), 1);
	setenv("SERVER_ADDR", SERVER_ADDR.c_str(), 1);
	setenv("SERVER_PORT", SERVER_PORT.c_str(), 1);
	setenv("REMOTE_ADDR", REMOTE_ADDR.c_str(), 1);
	setenv("REMOTE_PORT", REMOTE_PORT.c_str(), 1);
	setenv("EXEC_FILE", EXEC_FILE.c_str(), 1);
	#endif
}

void Session::PrintEnv()
{
	#ifdef __linux__
	REQUEST_METHOD = getenv("REQUEST_METHOD");
	REQUEST_URI = getenv("REQUEST_URI");
	QUERY_STRING = getenv("QUERY_STRING");
	SERVER_PROTOCOL = getenv("SERVER_PROTOCOL");
	HTTP_HOST = getenv("HTTP_HOST");
	SERVER_ADDR = getenv("SERVER_ADDR");
	SERVER_PORT = getenv("SERVER_PORT");
	REMOTE_ADDR = getenv("REMOTE_ADDR");
	REMOTE_PORT = getenv("REMOTE_PORT");
	EXEC_FILE = getenv("EXEC_FILE");
	#endif

	std::cout << "--------------------------------------------" << std::endl;
	std::cout << "REQUEST_METHOD: " << REQUEST_METHOD << std::endl;
	std::cout << "REQUEST_URI: " << REQUEST_URI << std::endl;
	std::cout << "QUERY_STRING: " << QUERY_STRING << std::endl;
	std::cout << "SERVER_PROTOCOL: " << SERVER_PROTOCOL << std::endl;
	std::cout << "HTTP_HOST: " << HTTP_HOST << std::endl;
	std::cout << "SERVER_ADDR: " << SERVER_ADDR << std::endl;
	std::cout << "SERVER_PORT: " << SERVER_PORT << std::endl;
	std::cout << "REMOTE_ADDR: " << REMOTE_ADDR << std::endl;
	std::cout << "REMOTE_PORT: " << REMOTE_PORT << std::endl;
	std::cout << "EXEC_FILE: " << EXEC_FILE << std::endl;
	std::cout << "--------------------------------------------" << std::endl;
}

void Session::PrintSOCK4Information(std::string S_IP, std::string S_PORT, std::string D_IP, std::string D_PORT, std::string command, std::string reply)
{
	printf("---------------------------\n");
	printf("<S_IP>: %s \n", S_IP.c_str());
	printf("<S_PORT>: %s \n", S_PORT.c_str());
	printf("<D_IP>: %s\n", D_IP.c_str());
	printf("<D_PORT>: %s\n", D_PORT.c_str());
	printf("<Command>: %s\n", command.c_str());
	printf("<Reply>: %s\n", reply.c_str());
	printf("---------------------------\n");
}

void Session::ParseSOCK4Request(int length)
{
	unsigned char USER_ID[1024];
	unsigned char DOMAIN_NAME[1024];

	D_PORT = std::to_string((int)data_[2] * 256 + (int)(data_[3] < 0 ? data_[3] + 256 : data_[3]));

	D_IP = "";
	for (int i = 4; i < 8; ++i)
	{
		if (i != 4) D_IP += ".";
			
		int temp = (data_[i] < 0 ? (int)data_[i] + 256 : data_[i]);
		D_IP += std::to_string(temp);
	}

	bool flag = false;
	int count = 0;
	for (int i = 8; i < (int)length; ++i)
	{
		if (!data_[i])
		{
			flag = true;
			count = 0;
		}
		else if (!flag)
		{
			USER_ID[count] = data_[i];
			USER_ID[count + 1] = '\0'; 
			++count;
		}
		else
		{
			DOMAIN_NAME[count] = data_[i];
			DOMAIN_NAME[count + 1] = '\0';
			++count;
		}			
	}
	
}

void Session::DoRead()
{
	auto self(shared_from_this());

	memset(data_, 0, max_length);
	socket_.async_read_some(boost::asio::buffer(data_, max_length),
		[this, self](boost::system::error_code ec, std::size_t length)
		{
			if (ec) return;

			ParseSOCK4Request(length);	
			DoReply();
		});
}

void Session::DoReadFromClient()
{
	auto self(shared_from_this());

	memset(reply_from_client, 0, max_length);
	socket_.async_read_some(boost::asio::buffer(reply_from_client),
		[this, self](boost::system::error_code ec, std::size_t length)
		{
			if (ec) return;
								
			DoRequestToWeb(length);
		});
			
}

void Session::DoReadFromWeb()
{
	auto self(shared_from_this());

	memset(reply_from_web, 0, max_length);
	(*web_socket).async_read_some(boost::asio::buffer(reply_from_web),
		[this, self](boost::system::error_code ec, std::size_t length)
		{
			if (ec) return;

			DoWriteToClient(length);
		});
}

void Session::DoReply()
{
	std::string command;
	std::string reply;

	if (data_[0] != 0x04)
	{
		reply = "Reject";

		DoReplyReject();	
	}
	else if (data_[1] == 0x01)
	{
		command = "CONNECT";
		reply = "Accept";
		
		DoReplyConnect();
	}	
	else if (data_[1] == 0x02)
	{
		command = "BIND";
		reply = "Accept";
		
		DoReplyBind();	
	}
	
	PrintSOCK4Information(
		socket_.remote_endpoint().address().to_string(),
		std::to_string(socket_.remote_endpoint().port()),
		D_IP, D_PORT, command, reply);
}

void Session::DoReplyReject()
{
	message[0] = 0;
	message[1] = 0x5B;

	auto self(shared_from_this());
	boost::asio::async_write(socket_, boost::asio::buffer(message, 8),
		[this, self](boost::system::error_code ec, std::size_t)
		{
			if (ec) return;

			web_socket = new tcp::socket(*Session::io_context_);
			
			tcp::endpoint endpoint(boost::asio::ip::address::from_string(D_IP), atoi((D_PORT).c_str()));
	
			(*web_socket).connect(endpoint);

			DoReadFromWeb();		
			DoReadFromClient();
		});

}

void Session::DoReplyConnect()
{
	message[0] = 0;
	message[1] = 0x5A;

	for (int i = 2; i < 8; ++i) message[i] = data_[i];

	auto self(shared_from_this());
	boost::asio::async_write(socket_, boost::asio::buffer(message, 8),
		[this, self](boost::system::error_code ec, std::size_t)
		{
			if (ec) return;

			web_socket = new tcp::socket(*Session::io_context_);
			
			tcp::endpoint endpoint(boost::asio::ip::address::from_string(D_IP), atoi((D_PORT).c_str()));
	
			(*web_socket).connect(endpoint);

			DoReadFromWeb();		
			DoReadFromClient();
		});

}

void Session::DoReplyBind()
{
	int temp;
	
	message[0] = 0;
	message[1] = 0x5A;

	tcp::endpoint endpoint(boost::asio::ip::address::from_string("0.0.0.0"), 0);

	acceptor_ = new tcp::acceptor(*Session::io_context_);

	(*acceptor_).open(tcp::v4());
	(*acceptor_).set_option(tcp::acceptor::reuse_address(true));
	(*acceptor_).bind(endpoint);
	(*acceptor_).listen(boost::asio::socket_base::max_connections);

	temp = (*acceptor_).local_endpoint().port() / 256;
	message[2] = (temp > 128 ? temp - 256 : temp);
	temp = (*acceptor_).local_endpoint().port() % 256;
	message[3] = (temp > 128 ? temp - 256 : temp);

	for (int i = 4; i < 8; ++i) message[i] = 0;

	auto self(shared_from_this());
	boost::asio::async_write(socket_, boost::asio::buffer(message, 8),
		[this, self](boost::system::error_code ec, std::size_t)
		{
			if (ec) return;

			web_socket = new tcp::socket(*Session::io_context_);
			
			(*acceptor_).accept(*web_socket);

			boost::asio::write(socket_, boost::asio::buffer(message, 8));

			DoReadFromWeb();
			DoReadFromClient();
		});

}

void Session::DoWriteToClient(int length)
{
	auto self(shared_from_this());
	boost::asio::async_write(socket_, boost::asio::buffer(reply_from_web, length),
		[this, self](boost::system::error_code ec, std::size_t len)
		{
			if (ec) return;
			//DoCGI();
			DoReadFromWeb();
		});
}

void Session::DoRequestToWeb(int length)
{
	auto self(shared_from_this());

	boost::asio::async_write((*web_socket), boost::asio::buffer(reply_from_client, length),
		[this, self](boost::system::error_code ec, std::size_t len)
		{
			if (ec) return;
			DoReadFromClient();
		});
}

void Session::DoCGI()
{
	(*Session::io_context_).notify_fork(boost::asio::io_context::fork_prepare);

	if (fork() != 0)
	{
		(*Session::io_context_).notify_fork(boost::asio::io_context::fork_parent);
		socket_.close();
	}
	else
	{
		(*Session::io_context_).notify_fork(boost::asio::io_context::fork_child);
		int sock = socket_.native_handle();
		
		dup2(sock, STDERR_FILENO);
		dup2(sock, STDIN_FILENO);
		dup2(sock, STDOUT_FILENO);

		socket_.close();

		if (execlp(EXEC_FILE.c_str(), EXEC_FILE.c_str(), NULL) < 0)
		{
			std::cout << "Content-type:text/html\r\n\r\n<h1>Fail</h1>";
			fflush(stdout);
		}
	}
}

Server::Server(boost::asio::io_context& io_context, short port)
		: acceptor_(io_context, tcp::endpoint(tcp::v4(), port))
{
	Session::SetContext(&io_context);

	DoAccept();
}

void Server::DoAccept()
{
	acceptor_.async_accept(
		[this](boost::system::error_code ec, tcp::socket socket)
		{
			if (ec) return;
			
			(*Session::io_context_).notify_fork(boost::asio::io_context::fork_prepare);

			if (fork() != 0)
			{
				(*Session::io_context_).notify_fork(boost::asio::io_context::fork_parent);
				socket.close();
				DoAccept();
			}
			else
			{
				(*Session::io_context_).notify_fork(boost::asio::io_context::fork_child);
				std::make_shared<Session>(std::move(socket))->Start();
			}
		}
	);
}
