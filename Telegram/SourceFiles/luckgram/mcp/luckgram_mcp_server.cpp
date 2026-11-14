/*
This file is part of Luckgram Desktop, based on Telegram Desktop.

Luckgram changes are licensed under GPL-3.0 like the upstream project.
*/
#include "luckgram/mcp/luckgram_mcp_server.h"

#include <QtCore/QTimer>
#include <QtNetwork/QHostAddress>
#include <QtNetwork/QTcpServer>
#include <QtNetwork/QTcpSocket>

namespace Luckgram::Mcp {
namespace {

constexpr auto kMaxConnections = 16;
constexpr auto kRequestTimeout = 10 * 1000; // ms, for the whole request.

void Reply(QTcpSocket *socket, const Response &response) {
	socket->write(Serialize(response));
	socket->disconnectFromHost();
}

} // namespace

Server::Server(Backend backend, std::function<QString()> token)
: _backend(std::move(backend))
, _token(std::move(token))
, _connections(std::make_shared<int>(0))
, _server(std::make_unique<QTcpServer>()) {
	const auto raw = _server.get();
	QObject::connect(raw, &QTcpServer::newConnection, raw, [=, this] {
		while (const auto socket = raw->nextPendingConnection()) {
			accept(socket);
		}
	});
}

Server::~Server() = default;

bool Server::listen(int firstPort, int attempts) {
	for (auto i = 0; i != attempts; ++i) {
		const auto port = firstPort + i;
		if (port < 1 || port > 65535) {
			break;
		} else if (_server->listen(QHostAddress::LocalHost, quint16(port))) {
			return true;
		}
	}
	return false;
}

int Server::port() const {
	return _server->isListening() ? int(_server->serverPort()) : 0;
}

QString Server::errorString() const {
	return _server->errorString();
}

void Server::accept(QTcpSocket *socket) {
	if (*_connections >= kMaxConnections) {
		socket->abort();
		socket->deleteLater();
		return;
	}
	++*_connections;
	// The sockets are children of the QTcpServer and go with it; the
	// counter is shared so a socket that outlives this object is harmless.
	const auto connections = _connections;
	QObject::connect(socket, &QTcpSocket::disconnected, socket, [=] {
		--*connections;
		socket->deleteLater();
	});
	// One request per connection; whatever happens, it ends in time.
	QTimer::singleShot(kRequestTimeout, socket, [=] {
		if (socket->state() != QAbstractSocket::UnconnectedState) {
			socket->abort();
		}
	});
	const auto buffer = std::make_shared<QByteArray>();
	const auto answered = std::make_shared<bool>(false);
	QObject::connect(socket, &QTcpSocket::readyRead, socket, [=, this] {
		if (*answered) {
			socket->readAll();
			return;
		}
		buffer->append(socket->readAll());
		auto request = Request();
		auto status = 400;
		const auto parsed = ParseRequest(*buffer, &request, &status);
		if (parsed == ParseResult::Incomplete) {
			return;
		}
		*answered = true;
		if (parsed == ParseResult::Bad) {
			auto response = Response();
			response.status = status;
			Reply(socket, response);
			return;
		}
		auto config = Config();
		config.token = _token();
		config.port = port();
		Reply(socket, Handle(request, config, _backend));
	});
}

} // namespace Luckgram::Mcp
