/*
 * Copyright (c) 2012 Mike Sharkey <michael_sharkey@firstclass.com>
 *
 * "THE BEER-WARE LICENSE" (Revision 42):
 * Mike Sharkey wrote this file. As long as you retain this notice you
 * can do whatever you want with this stuff. If we meet some day, and you think
 * this stuff is worth it, you can buy me a beer in return.
 */
#include "opennicresolver.h"

#include <QObject>
#include <QMessageBox>
#include <QProcess>
#include <QString>
#include <QStringList>
#include <QEventLoop>
#include <QProgressDialog>
#include <QByteArray>
#include <QSettings>
#include <QSpinBox>
#include <QFile>

OpenNICResolver::OpenNICResolver(QObject *parent)
: QObject(parent)
{
	QObject::connect(&mTest,SIGNAL(queryResult(OpenNICTest::query*)),this,SLOT(insertResult(OpenNICTest::query*)));
	mTimer = startTimer(1000*1);
}

OpenNICResolver::~OpenNICResolver()
{
	killTimer(mTimer);
}

/**
  * @brief Test results come in here.
  */
void OpenNICResolver::insertResult(OpenNICTest::query *result)
{
	if ( result != NULL )
	{
		QString ip = result->addr.toString();
		quint64 latency = result->latency;
		QMutableMapIterator<quint64,QString>i(mResolvers);
		while (i.hasNext())
		{
			i.next();
			if ( i.value() == ip )
			{
				if ( latency > 0 && result->error == 0 )
				{
					i.remove();
					mResolvers.insert(latency,ip);
					break;
				}
			}
		}
	}
}

/**
  * @brief Evaluate the peformance of resolver.
  */
void OpenNICResolver::evaluateResolver()
{
	QStringList domains = getDomains();
	if ( !domains.empty() && !mResolvers.empty() )
	{
		QString domain = domains.at(randInt(0,domains.count()-1));
		int resolverIdx = randInt(0,mResolvers.count()-1);
		QMutableMapIterator<quint64,QString>i(mResolvers);
		while (i.hasNext())
		{
			i.next();
			if ( --resolverIdx < 0 )
			{
				QString ip = i.value();
				QHostAddress addr(ip);
				mTest.resolve(addr,domain);
				break;
			}
		}
	}
}

/**
  * @brief Get a default T1 list from the bootstrap file.
  * @return A string list of IP numbers representing potential T1s.
  */
QStringList OpenNICResolver::defaultT1List()
{
	QStringList rc;
	QFile file(OPENNIC_T1_BOOTSTRAP);
	if ( file.open(QIODevice::ReadOnly) )
	{
		while (!file.atEnd()) {
			QByteArray line = file.readLine();
			QString ip(line);
			if ( !ip.trimmed().isEmpty() )
			{
				rc << ip.trimmed();
			}
		}
		file.close();
	}
	if ( !rc.count() )
	{
		/** a last ditch effort... */
		rc << "72.232.162.195";
		rc << "216.87.84.210";
		rc << "199.30.58.57";
		rc << "128.177.28.254";
		rc << "207.192.71.13";
		rc << "66.244.95.11";
		rc << "178.63.116.152";
		rc << "202.83.95.229";
	}
	return rc;
}

/**
  * @brief Get a default domains list from the bootstrap file.
  * @return A string list of domains to test.
  */
QStringList OpenNICResolver::getDomains()
{
	if ( mDomains.empty() )
	{
		QStringList rc;
		QFile file(OPENNIC_DOMAINS_BOOTSTRAP);
		if ( file.open(QIODevice::ReadOnly) )
		{
			while (!file.atEnd()) {
				QByteArray line = file.readLine();
				QString ip(line);
				if ( !ip.trimmed().isEmpty() )
				{
					rc << ip.trimmed();
				}
			}
			file.close();
		}
		if ( !rc.count() )
		{
			/** a last ditch effort... */
			rc << "dns.opennic.glue";
			rc << "register.bbs";
			rc << "for.free";
			rc << "grep.geek";
			rc << "register.ing";
			rc << "google.com";
			rc << "yahoo.com";
			rc << "wikipedia.com";
		}
		return rc;
	}
	return mDomains;
}

/**
  * @brief Fetch the list of DNS resolvers and return them as strings.
  */
QStringList OpenNICResolver::getResolvers()
{
	QStringList ips;
	/* If there are current no resolvers in the map table, then try to populate it... */
	if ( mResolvers.isEmpty() )
	{
		initializeResolvers();		/* fetch the intial list of T2s */
	}
	if ( mResolvers.isEmpty() )
	{
		ips = defaultT1List();		/* something is wrong - return the T1s */
	}
	else
	{
		/** sort the latenct times in ascending order */
		QMutableMapIterator<quint64,QString>i(mResolvers);
		while (i.hasNext())
		{
			i.next();
			ips.append(i.value());
		}
	}
	return ips;
}

/**
  * @brief Add a dns entry to the system's list of DNS resolvers.
  */
QString OpenNICResolver::addResolver(QString dns,int index)
{
	QString rc;
	QEventLoop loop;
	QString program = "netsh";
	QStringList arguments;
	if ( index == 1 )
	{
		arguments << "interface" << "ip" << "set" << "dns" << "Local Area Connection" << "static" << dns;
	}
	else
	{
		arguments << "interface" << "ip" << "add" << "dns" << "Local Area Connection" << dns << "index="+QString::number(index);
	}
	QProcess *process = new QProcess(this);
	process->start(program, arguments);
	while (process->waitForFinished(3000))
	{
		loop.processEvents();
	}
	rc = process->readAllStandardOutput().trimmed() + "\n";
	delete process;
	return rc;
}

/**
  * @brief Get the text which will show the current DNS resolver settings.
  */
QString OpenNICResolver::getSettingsText()
{
	QByteArray output;
	QEventLoop loop;
	QString program = "netsh";
	QStringList arguments;
	arguments << "interface" << "ip" << "show" << "config" << "Local Area Connection";
	QProcess *process = new QProcess(this);
	process->start(program, arguments);
	while (process->waitForFinished(10000))
	{
		loop.processEvents();
	}
	output = process->readAllStandardOutput();
	delete process;
	return output;
}

/**
  * @brief Fetch the raw list of DNS resolvers and return them as strings.
  */
QStringList OpenNICResolver::getBootstrapResolverList()
{
	QStringList outputList;
	QStringList ips;
	QEventLoop loop;
	QString program = "dig";
	QStringList arguments;
	QString output;
	arguments << "dns.opennic.glue" << "+short";
	QProcess *process = new QProcess(this);
	process->start(program, arguments);
	while (process->waitForFinished(10000))
	{
		loop.processEvents();
	}
	output = process->readAllStandardOutput();
	if (output.isEmpty())
	{
		output = process->readAllStandardError();
	}
	outputList = output.trimmed().split('\n');
	for(int n=0; n < outputList.count(); n++)
	{
		QString ip = outputList.at(n).trimmed();
		if (ip.at(0) >= '0' && ip.at(0) <= '9')
		{
			ips.append(ip);
		}
	}
	return ips;
}

/**
  * @brief Fetch the raw list of resolvers and insert into the map table.
  */
void OpenNICResolver::initializeResolvers()
{
	QStringList ips = getBootstrapResolverList();
	for(int n=0; n < ips.count(); n++)
	{
		QString ip = ips.at(n);
		mResolvers.insert((quint64)10000+n,ip);	/* simulate latency for the initial bootstrap */
	}
}

/**
  * @brief Generate a randome number.between low and high
  */
int OpenNICResolver::randInt(int low, int high)
{
	return qrand()%((high+1)-low)+low;
}

/**
  * @brief Get here on timer events
  */
void OpenNICResolver::timerEvent(QTimerEvent* e)
{
	if ( e->timerId() == mTimer )
	{
		evaluateResolver();
	}
}

