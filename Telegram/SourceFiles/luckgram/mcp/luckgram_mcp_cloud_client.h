/*
This file is part of Luckgram Desktop, based on Telegram Desktop.

Luckgram changes are licensed under GPL-3.0 like the upstream project.
*/
#pragma once

#include "luckgram/mcp/luckgram_mcp_logic.h"

#include <functional>
#include <memory>

class QNetworkAccessManager;
class QObject;

// Cloud MCP: the long poll to mcp.luckgram.cc. Takes the JSON-RPC messages
// the relay hands over, answers them with the same logic as the local
// server and sends the answers with the next poll.
// QtCore + QtNetwork + std only, so the same file runs against the real
// relay outside the app.
namespace Luckgram::Mcp {

class CloudClient final {
public:
	struct Config {
		QString key;
		QString deviceId;
		QString deviceName;
		QString version;
	};

	// config is read at every poll: a new key or name is used at once.
	// changed fires when connected() or lastError() changed.
	CloudClient(
		Backend backend,
		std::function<Config()> config,
		std::function<void()> changed);
	~CloudClient();

	void start();
	void stop();

	[[nodiscard]] bool running() const;
	// The last poll was answered by the relay.
	[[nodiscard]] bool connected() const;
	[[nodiscard]] QString lastError() const;

private:
	void poll();
	void retryLater();
	void setState(bool connected, const QString &error);

	const Backend _backend;
	const std::function<Config()> _config;
	const std::function<void()> _changed;
	// Everything asynchronous is bound to this object: gone with the
	// client, so a late answer or reply touches nothing.
	const std::unique_ptr<QObject> _context;
	const std::unique_ptr<QNetworkAccessManager> _network;
	std::vector<CloudResult> _results;
	int _generation = 0;
	int _failures = 0;
	bool _running = false;
	bool _connected = false;
	bool _retryScheduled = false;
	QString _error;

};

} // namespace Luckgram::Mcp
