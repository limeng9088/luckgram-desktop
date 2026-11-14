/*
This file is part of Luckgram Desktop, based on Telegram Desktop.

Luckgram changes are licensed under GPL-3.0 like the upstream project.
*/
#pragma once

#include "luckgram/mcp/luckgram_mcp_logic.h"

#include <functional>
#include <memory>

class QTcpServer;
class QTcpSocket;

// The sockets of the local MCP server: 127.0.0.1 only, one request per
// connection, a few connections at a time, each ended after a few seconds.
// QtCore + QtNetwork + std only, so the same file runs under a real MCP
// client outside the app. What a request means is in luckgram_mcp_logic.h.
namespace Luckgram::Mcp {

class Server final {
public:
	// The token is asked for at every request: a new one works at once.
	Server(Backend backend, std::function<QString()> token);
	~Server();

	// The first free port of firstPort .. firstPort + attempts - 1.
	[[nodiscard]] bool listen(int firstPort, int attempts);
	[[nodiscard]] int port() const; // 0 when not listening.
	[[nodiscard]] QString errorString() const;

private:
	void accept(QTcpSocket *socket);

	const Backend _backend;
	const std::function<QString()> _token;
	const std::shared_ptr<int> _connections;
	const std::unique_ptr<QTcpServer> _server;

};

} // namespace Luckgram::Mcp
