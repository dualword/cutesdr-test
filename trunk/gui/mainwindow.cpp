/////////////////////////////////////////////////////////////////////
// mainwindow.cpp: implementation of the mainwindow class.
//
//	This class implements the top level GUI control and is the primary
// object instantiated by main().  All other classes and threads are
// spawned from this module.
//
// History:
//	2010-09-15  Initial creation MSW
//	2011-03-27  Initial release
//	2011-04-16  Added Frequency range logic
//	2011-05-26  Added support for In Use Status allowed wider sideband filters
//	2011-08-07  Added WFM Support and spectrum inversion
//	2012-01-05  Changed CW offset limits, changed scope resolution operators in downconverter module
//	2012-02-11  ver 1.05 Updated to QT 4.8 and fixed issue with not remembering the span setting
//	2012-06-01  ver 1.06 fixed threading issue with txmsg
//	2013-03-25  ver 1.10 Updated to QT 5.01 changed threading methods, split GUI forms by OS
//	2013-07-28  ver 1.11 Updated to QT 5.10 fixed DisconnectFromServerSlot bug, Added single/double precision math macros
//	2013-12-16  ver 1.12 Updated to QT 5.20 updated to Q_OS_WIN macro use
//	2014-02-23  ver 1.13 Updated to correct qwindows.dll for QT 5.2 and expanded frequency ranges
//	2014-07-11  ver 1.14 Updated for QT 5.3
//	2014-09-22  ver 1.15 Modified Thread Launcher to deal with closing resources from thread context
//	2015-02-05  ver 1.15a Added CloudSDR discover fields. no need for release now
//	2015-02-25  ver 1.16 Added CloudSDR-IQ and added PSK digital decoder
//	2015-03-26  ver 1.17 Added  support for small MTU and UDP keepalive in case of port forwarding timeouts
//	2015-06-09  ver 1.18 Fixed discovery issue with CloudIQ
//	2015-08-27  ver 1.19 Changed Cloudxx max BW to 1.5MHz, added wav file saving
//	2016-01-10  ver 1.20b1 Removed x86 assembly code in wfmdemod, added settings saving in OnExit() (thanks alex for fix)
//	2016-01-10  ver 1.20b2 Removed another x86 assembly code in wfmdemod changed StayOnTop inhibit for Linux
//	2016-10-14  ver 1.20b3 Release candidate
//	2017-09-09  ver 1.20b4 Added network Interface selection
//	2017-09-14  ver 1.20b5 Added FSK demod but not finished
//	2017-12-25  ver 1.20 Cleaned up DSC mode, recompile with Qt 5.10
//	2018-04-24  ver 1.21b0 Adding File transmit capability

/////////////////////////////////////////////////////////////////////
//==========================================================================================
// + + +   This Software is released under the "Simplified BSD License"  + + +
//Copyright 2010 Moe Wheatley. All rights reserved.
//
//Redistribution and use in source and binary forms, with or without modification, are
//permitted provided that the following conditions are met:
//
//   1. Redistributions of source code must retain the above copyright notice, this list of
//	  conditions and the following disclaimer.
//
//   2. Redistributions in binary form must reproduce the above copyright notice, this list
//	  of conditions and the following disclaimer in the documentation and/or other materials
//	  provided with the distribution.
//
//THIS SOFTWARE IS PROVIDED BY Moe Wheatley ``AS IS'' AND ANY EXPRESS OR IMPLIED
//WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND
//FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL Moe Wheatley OR
//CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
//CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR
//SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON
//ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING
//NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF
//ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
//
//The views and conclusions contained in the software and documentation are those of the
//authors and should not be interpreted as representing official policies, either expressed
//or implied, of Moe Wheatley.
//==========================================================================================

#include <QMainWindow>
#include "gui/mainwindow.h"
#include "ui_mainwindow.h"
#include <QDebug>
#include "gui/freqctrl.h"
#include "gui/editnetdlg.h"
#include "gui/sdrsetupdlg.h"
#include "gui/noiseprocdlg.h"
#include "gui/sounddlg.h"
#include "gui/displaydlg.h"
#include "gui/aboutdlg.h"
#include "gui/recordsetupdlg.h"
#include "gui/filetxdlg.h"
#include "interface/perform.h"

/*---------------------------------------------------------------------------*/
/*--------------------> L O C A L   D E F I N E S <--------------------------*/
/*---------------------------------------------------------------------------*/
#define PROGRAM_TITLE_VERSION tr(" 1.21 beta0")

#define MAX_FFTDB 60
#define MIN_FFTDB -170


/////////////////////////////////////////////////////////////////////
// Constructor/Destructor
/////////////////////////////////////////////////////////////////////
MainWindow::MainWindow(QWidget *parent) :
    QMainWindow(parent),
    ui(new Ui::MainWindow)
{
	ui->setupUi(this);
	m_ProgramExeName = QFileInfo(QApplication::applicationFilePath()).fileName();
	m_ProgramExeName.remove(".exe", Qt::CaseInsensitive);
	setWindowTitle(m_ProgramExeName + PROGRAM_TITLE_VERSION);

	//create SDR interface class
	m_pSdrInterface = new CSdrInterface;
	//give GUI plotter access to the sdr interface object
	ui->framePlot->SetSdrInterface(m_pSdrInterface);

	//create the global Testbench object
	if(!g_pTestBench)
		g_pTestBench = new CTestBench(this);

	if(!g_pChatDialog)
	{
		g_pChatDialog = new CChatDialog(this, Qt::WindowTitleHint );
		g_pChatDialog->SetSdrInterface(m_pSdrInterface);
	}

	InitDemodSettings();	//must be before readSettings to set some defualts
	readSettings();			//read persistent settings

#ifndef Q_OS_LINUX
	ui->actionAlwaysOnTop->setChecked(m_AlwaysOnTop);
	AlwaysOnTop();
#endif

	//create Demod setup menu for non-modal use(can leave up and still access rest of program)
	m_pDemodSetupDlg = new CDemodSetupDlg(this);

	m_pTimer = new QTimer(this);

	//connect a bunch of signals to the GUI objects
	connect(m_pTimer, SIGNAL(timeout()), this, SLOT(OnTimer()));

	connect(ui->frameFreqCtrl, SIGNAL(NewFrequency(qint64)), this, SLOT(OnNewCenterFrequency(qint64)));
	connect(ui->frameDemodFreqCtrl, SIGNAL(NewFrequency(qint64)), this, SLOT(OnNewDemodFrequency(qint64)));

	connect(m_pSdrInterface, SIGNAL(NewStatus(int)), this,  SLOT( OnStatus(int) ) );
	connect(m_pSdrInterface, SIGNAL(NewInfoData()), this,  SLOT( OnNewInfoData() ) );
	connect(m_pSdrInterface, SIGNAL(NewFftData()), this,  SLOT( OnNewFftData() ) );

	connect(ui->actionExit, SIGNAL(triggered()), this, SLOT(OnExit()));
	connect(ui->actionNetwork, SIGNAL(triggered()), this, SLOT(OnNetworkDlg()));
	connect(ui->actionSoundCard, SIGNAL(triggered()), this, SLOT(OnSoundCardDlg()));
	connect(ui->actionSDR, SIGNAL(triggered()), this, SLOT(OnSdrDlg()));
	connect(ui->actionDisplay, SIGNAL(triggered()), this, SLOT(OnDisplayDlg()));
	connect(ui->actionAlwaysOnTop, SIGNAL(triggered()), this, SLOT(AlwaysOnTop()));
	connect(ui->actionDemod_Setup, SIGNAL(triggered()), this, SLOT(OnDemodDlg()));
	connect(ui->actionNoise_Processing, SIGNAL(triggered()), this, SLOT(OnNoiseProcDlg()));
	connect(ui->actionRecordSetup,SIGNAL(triggered()), this, SLOT(OnRecordSetupDlg()));
	connect(ui->actionFile_Send,SIGNAL(triggered()), this, SLOT(OnFileSendDlg()));


	connect(ui->actionAbout, SIGNAL(triggered()), this, SLOT(OnAbout()));

	connect(ui->framePlot, SIGNAL(NewDemodFreq(qint64)), this,  SLOT( OnNewScreenDemodFreq(qint64) ) );
	connect(ui->framePlot, SIGNAL(NewCenterFreq(qint64)), this,  SLOT( OnNewScreenCenterFreq(qint64) ) );
    connect(ui->framePlot, SIGNAL(NewLowCutFreq(int)), this,  SLOT( OnNewLowCutFreq(int) ) );
    connect(ui->framePlot, SIGNAL(NewHighCutFreq(int)), this,  SLOT( OnNewHighCutFreq(int) ) );

	m_pTimer->start(200);		//start up status timer

	m_pSdrInterface->SetRadioType(m_RadioType);
		quint32 maxspan = m_pSdrInterface->GetMaxBWFromIndex(m_BandwidthIndex);

	ui->framePlot->SetPercent2DScreen(m_Percent2DScreen);

	//initialize controls and limits
	qint32 tmpspan = m_SpanFrequency;	//save since setting range triggers control to update
	ui->SpanspinBox->setMaximum(maxspan/1000);
	m_SpanFrequency = tmpspan;
	if(m_SpanFrequency>maxspan)
		m_SpanFrequency = maxspan;
	ui->SpanspinBox->setValue(m_SpanFrequency/1000);
	m_LastSpanKhz = m_SpanFrequency/1000;

	//tmp save demod freq since gets set to center freq by center freq control inititalization
	qint64 tmpdemod = m_DemodFrequency;

	ui->frameFreqCtrl->Setup(10, 100U, 1700000000U, 1, UNITS_KHZ );
	ui->frameFreqCtrl->SetBkColor(Qt::darkBlue);
	ui->frameFreqCtrl->SetDigitColor(Qt::cyan);
	ui->frameFreqCtrl->SetUnitsColor(Qt::lightGray);
	ui->frameFreqCtrl->SetHighlightColor(Qt::darkGray);
	ui->frameFreqCtrl->SetFrequency(m_CenterFrequency);

	m_DemodFrequency = tmpdemod;
	ui->frameDemodFreqCtrl->Setup(10, 100U, 1700000000U, 1, UNITS_KHZ );
	ui->frameDemodFreqCtrl->SetBkColor(Qt::darkBlue);
	ui->frameDemodFreqCtrl->SetDigitColor(Qt::white);
	ui->frameDemodFreqCtrl->SetUnitsColor(Qt::lightGray);
	ui->frameDemodFreqCtrl->SetHighlightColor(Qt::darkGray);
	//limit demod frequency to Center Frequency +/-span frequency
	ui->frameDemodFreqCtrl->Setup(10, m_CenterFrequency-m_SpanFrequency/2,
								  m_CenterFrequency+m_SpanFrequency/2,
								  1,
								  UNITS_KHZ );
	ui->frameDemodFreqCtrl->SetFrequency(m_DemodFrequency);

	ui->framePlot->SetSpanFreq( m_SpanFrequency );
	ui->framePlot->SetCenterFreq( m_CenterFrequency );
	ui->framePlot->EnableCurText(m_UseCursorText);
	m_FreqChanged = false;

	ui->horizontalSliderVol->setValue(m_Volume);
	m_pSdrInterface->SetVolume(m_Volume);

	ui->ScalecomboBox->addItem(tr("10 dB/Div"), 10);
	ui->ScalecomboBox->addItem(tr("5 dB/Div"), 5);
	ui->ScalecomboBox->addItem(tr("3 dB/Div"), 3);
	ui->ScalecomboBox->addItem(tr("1 dB/Div"), 1);
	m_dBStepSize = (int)ui->ScalecomboBox->itemData(m_VertScaleIndex).toInt();
	ui->ScalecomboBox->setCurrentIndex(m_VertScaleIndex);
	ui->framePlot->SetdBStepSize(m_dBStepSize);

	ui->MaxdBspinBox->setValue(m_MaxdB);
	ui->MaxdBspinBox->setSingleStep(m_dBStepSize);
	ui->MaxdBspinBox->setMinimum(MIN_FFTDB+VERT_DIVS*m_dBStepSize);
	ui->MaxdBspinBox->setMaximum(MAX_FFTDB);

	ui->framePlot->SetMaxdB(m_MaxdB);


	m_pSdrInterface->SetFftSize( m_FftSize);
	m_pSdrInterface->SetFftAve( m_FftAve);
	m_pSdrInterface->SetMaxDisplayRate(m_MaxDisplayRate);
	m_pSdrInterface->SetSdrBandwidthIndex(m_BandwidthIndex);
	m_pSdrInterface->SetSdrRfGain( m_RfGain );
	m_pSdrInterface->ManageNCOSpurOffsets(CSdrInterface::NCOSPUR_CMD_SET,
										  &m_NCOSpurOffsetI,
										  &m_NCOSpurOffsetQ);

	m_pSdrInterface->SetSoundCardSelection(m_SoundInIndex, m_SoundOutIndex, m_StereoOut);
	m_pSdrInterface->SetSpectrumInversion(m_InvertSpectrum);
	m_pSdrInterface->SetUSFmVersion(m_USFm);

	ui->framePlot->SetDemodCenterFreq( m_DemodFrequency );
	SetupDemod(m_DemodMode);
	m_RdsDecode.DecodeReset(m_USFm);

	m_pSdrInterface->SetDemod(m_DemodMode, m_DemodSettings[m_DemodMode]);

	SetupNoiseProc();

	UpdateInfoBox();

	m_ActiveDevice = tr("");
	m_Status = CSdrInterface::NOT_CONNECTED;
	m_LastStatus = m_Status;

	m_KeepAliveTimer = 0;

	if(m_UseTestBench)
	{
		//make sure top of dialog is visable(0,0 doesn't include menu bar.Qt bug?)
		if(m_TestBenchRect.top()<30)
			m_TestBenchRect.setTop(30);
		g_pTestBench->setGeometry(m_TestBenchRect);
		g_pTestBench->show();
		g_pTestBench->Init();
	}

	if( (DEMOD_PSK == m_DemodMode) || (DEMOD_FSK == m_DemodMode) )
		SetChatDialogState(true);

	StopRecord();

}

MainWindow::~MainWindow()
{
	if(g_pTestBench)
		delete g_pTestBench;
	if(m_pSdrInterface)
	{
		m_pSdrInterface->StopIO();
		delete m_pSdrInterface;
	}
	if(m_pDemodSetupDlg)
		delete m_pDemodSetupDlg;
	delete ui;

}

/////////////////////////////////////////////////////////////////////
// Called when program is closed to save persistant data
/////////////////////////////////////////////////////////////////////
void MainWindow::closeEvent(QCloseEvent *)
 {
	writeSettings();
	if(m_pSdrInterface)
		m_pSdrInterface->StopIO();
 }

/////////////////////////////////////////////////////////////////////
// Called to set "Always on Top" Main Window state
/////////////////////////////////////////////////////////////////////
void MainWindow::AlwaysOnTop()
{
#ifndef Q_OS_LINUX
	m_AlwaysOnTop = ui->actionAlwaysOnTop->isChecked();
	Qt::WindowFlags flags = this->windowFlags();

	if (m_AlwaysOnTop)
		this->setWindowFlags( (flags & ~Qt::WindowStaysOnBottomHint) | Qt::WindowStaysOnTopHint  | Qt::CustomizeWindowHint );
	else
		this->setWindowFlags( (flags & ~Qt::WindowStaysOnTopHint) | Qt::WindowStaysOnBottomHint  | Qt::CustomizeWindowHint );
	this->show();
#endif
}

/////////////////////////////////////////////////////////////////////
// Program persistant data save/recall methods
/////////////////////////////////////////////////////////////////////
void MainWindow::writeSettings()
{
	QSettings settings( QSettings::UserScope,tr("MoeTronix"), m_ProgramExeName);
	settings.beginGroup(tr("MainWindow"));

	settings.setValue(tr("geometry"), saveGeometry());
	settings.setValue(tr("minstate"),isMinimized());

	if(g_pTestBench->isVisible())
	{
		m_TestBenchRect = g_pTestBench->geometry();
		settings.setValue(tr("TestBenchRect"),m_TestBenchRect);
	}

	if( g_pChatDialog->isVisible() )
		m_ChatDialogRect = g_pChatDialog->geometry();
	settings.setValue(tr("ChatDialogRect"),m_ChatDialogRect);


	settings.endGroup();

	settings.beginGroup(tr("Common"));

	settings.setValue(tr("RadioType"), m_RadioType);
	settings.setValue(tr("CenterFrequency"),(int)m_CenterFrequency);
	settings.setValue(tr("TxFrequency"),(unsigned int)m_TxFrequency);
	settings.setValue(tr("SpanFrequency"),(int)m_SpanFrequency);
	settings.setValue(tr("IPAdr"),m_IPAdr.toIPv4Address());
	settings.setValue(tr("Port"),m_Port);
	settings.setValue(tr("RfGain"),m_RfGain);
	settings.setValue(tr("BandwidthIndex"), m_BandwidthIndex );
	settings.setValue(tr("SoundInIndex"),m_SoundInIndex);
	settings.setValue(tr("SoundOutIndex"),m_SoundOutIndex);
	settings.setValue(tr("StereoOut"),m_StereoOut);
	settings.setValue(tr("VertScaleIndex"),m_VertScaleIndex);
	settings.setValue(tr("MaxdB"),m_MaxdB);
	settings.setValue(tr("FftSize"),m_FftSize);
	settings.setValue(tr("FftAve"),m_FftAve);
	settings.setValue(tr("MaxDisplayRate"),m_MaxDisplayRate);
	settings.setValue(tr("UseTestBench"),m_UseTestBench);
	settings.setValue(tr("AlwaysOnTop"),m_AlwaysOnTop);
	settings.setValue(tr("Volume"),m_Volume);
	settings.setValue(tr("Percent2DScreen"),m_Percent2DScreen);
	settings.setValue(tr("ActiveHostAdrIndex"),m_ActiveHostAdrIndex);
	settings.setValue(tr("InvertSpectrum"),m_InvertSpectrum);
	settings.setValue(tr("USFm"),m_USFm);
	settings.setValue(tr("UseCursorText"),m_UseCursorText);
	settings.setValue("RecordFilePath", m_RecordFilePath);
	settings.setValue("TxFilePath", m_TxFilePath);
	settings.setValue(tr("TxRepeat"),m_TxRepeat);
	settings.setValue(tr("UseTxFile"),m_UseTxFile);


	settings.setValue("UseUdpFwd", m_UseUdpFwd);
	settings.setValue(tr("IPFwdAdr"),m_IPFwdAdr.toIPv4Address());
	settings.setValue(tr("FwdPort"),m_FwdPort);

	settings.setValue(tr("TxSignalPower"),m_TxSignalPower);
	settings.setValue(tr("TxNoisePower"),m_TxNoisePower);
	settings.setValue(tr("TxSweepStartFrequency"),m_TxSweepStartFrequency);
	settings.setValue(tr("TxSweepStopFrequency"),m_TxSweepStopFrequency);
	settings.setValue(tr("TxSweepRate"),m_TxSweepRate);

	//Get NCO spur offsets and save
	m_pSdrInterface->ManageNCOSpurOffsets(CSdrInterface::NCOSPUR_CMD_READ,
										  &m_NCOSpurOffsetI,
										  &m_NCOSpurOffsetQ);
	settings.setValue(tr("NCOSpurOffsetI"),m_NCOSpurOffsetI);
	settings.setValue(tr("NCOSpurOffsetQ"),m_NCOSpurOffsetQ);

	settings.setValue(tr("DemodFrequency"),(int)m_DemodFrequency);
	settings.setValue(tr("DemodMode"),m_DemodMode);
	settings.setValue(tr("RecordMode"),m_RecordMode);


	settings.setValue(tr("NBOn"),m_NoiseProcSettings.NBOn);
	settings.setValue(tr("NBThreshold"),m_NoiseProcSettings.NBThreshold);
	settings.setValue(tr("NBWidth"),m_NoiseProcSettings.NBWidth);

	settings.endGroup();

	settings.beginGroup(tr("Testbench"));

	settings.setValue(tr("SweepStartFrequency"),g_pTestBench->m_SweepStartFrequency);
	settings.setValue(tr("SweepStopFrequency"),g_pTestBench->m_SweepStopFrequency);
	settings.setValue(tr("SweepRate"),g_pTestBench->m_SweepRate);
	settings.setValue(tr("DisplayRate"),g_pTestBench->m_DisplayRate);
	settings.setValue(tr("VertRange"),g_pTestBench->m_VertRange);
	settings.setValue(tr("TrigIndex"),g_pTestBench->m_TrigIndex);
	settings.setValue(tr("TimeDisplay"),g_pTestBench->m_TimeDisplay);
	settings.setValue(tr("HorzSpan"),g_pTestBench->m_HorzSpan);
	settings.setValue(tr("TrigLevel"),g_pTestBench->m_TrigLevel);
	settings.setValue(tr("Profile"),g_pTestBench->m_Profile);
	settings.setValue(tr("GenOn"),g_pTestBench->m_GenOn);
	settings.setValue(tr("PeakOn"),g_pTestBench->m_PeakOn);
	settings.setValue(tr("PulseWidth"),g_pTestBench->m_PulseWidth);
	settings.setValue(tr("PulsePeriod"),g_pTestBench->m_PulsePeriod);
	settings.setValue(tr("SignalPower"),g_pTestBench->m_SignalPower);
	settings.setValue(tr("NoisePower"),g_pTestBench->m_NoisePower);
	settings.setValue(tr("GenMode"),g_pTestBench->m_GenMode);

	settings.endGroup();

	settings.beginWriteArray(tr("Demod"));
	//save demod settings
	for (int i = 0; i < NUM_DEMODS; i++)
	{
		settings.setArrayIndex(i);
		settings.setValue(tr("HiCut"), m_DemodSettings[i].HiCut);
		settings.setValue(tr("LowCut"), m_DemodSettings[i].LowCut);
		settings.setValue(tr("FreqClickResolution"), m_DemodSettings[i].FreqClickResolution);
		settings.setValue(tr("FilterClickResolution"), m_DemodSettings[i].FilterClickResolution);
		settings.setValue(tr("Offset"), m_DemodSettings[i].Offset);
		settings.setValue(tr("SquelchValue"), m_DemodSettings[i].SquelchValue);
		settings.setValue(tr("AgcSlope"), m_DemodSettings[i].AgcSlope);
		settings.setValue(tr("AgcThresh"), m_DemodSettings[i].AgcThresh);
		settings.setValue(tr("AgcManualGain"), m_DemodSettings[i].AgcManualGain);
		settings.setValue(tr("AgcDecay"), m_DemodSettings[i].AgcDecay);
		settings.setValue(tr("AgcOn"), m_DemodSettings[i].AgcOn);
		settings.setValue(tr("AgcHangOn"), m_DemodSettings[i].AgcHangOn);
	}
	settings.endArray();
}

void MainWindow::readSettings()
{
	QSettings settings(QSettings::UserScope,tr("MoeTronix"), m_ProgramExeName);
	settings.beginGroup(tr("MainWindow"));

	restoreGeometry(settings.value(tr("geometry")).toByteArray());
	bool ismin = settings.value(tr("minstate"), false).toBool();
	m_TestBenchRect = settings.value(tr("TestBenchRect"), QRect(0,0,500,200)).toRect();

	m_ChatDialogRect = settings.value(tr("ChatDialogRect"), QRect(10,10,500,200)).toRect();
	if( (m_ChatDialogRect.x()<0) ||  (m_ChatDialogRect.y()<0) )
	{
		 m_ChatDialogRect.setX(10);
		 m_ChatDialogRect.setY(10);
	}
	settings.endGroup();

	settings.beginGroup(tr("Common"));

	m_CenterFrequency = (qint64)settings.value(tr("CenterFrequency"), 15000000).toUInt();
	m_TxFrequency = (qint64)settings.value(tr("TxFrequency"), 15000000).toUInt();
	m_SpanFrequency = settings.value(tr("SpanFrequency"), 100000).toUInt();
	m_IPAdr.setAddress(settings.value(tr("IPAdr"), 0xC0A80164).toInt() );
	m_Port = settings.value(tr("Port"), 50000).toUInt();
	m_IPFwdAdr.setAddress(settings.value(tr("IPFwdAdr"), 0xC0A80164).toInt() );
	m_FwdPort = settings.value(tr("FwdPort"), 50010).toUInt();
	m_RfGain = settings.value(tr("RfGain"), 0).toInt();
	m_BandwidthIndex = settings.value(tr("BandwidthIndex"), 0).toInt();
	m_SoundInIndex = settings.value(tr("SoundInIndex"), 0).toInt();
	m_SoundOutIndex = settings.value(tr("SoundOutIndex"), 0).toInt();
	m_StereoOut = settings.value(tr("StereoOut"), false).toBool();
	m_VertScaleIndex = settings.value(tr("VertScaleIndex"), 0).toInt();
	m_MaxdB = settings.value(tr("MaxdB"), 0).toInt();
	m_FftAve = settings.value(tr("FftAve"), 0).toInt();
	m_FftSize = settings.value(tr("FftSize"), 4096).toInt();
	m_MaxDisplayRate = settings.value(tr("MaxDisplayRate"), 10).toInt();
	m_RadioType = settings.value(tr("RadioType"), 0).toInt();
	m_Volume = settings.value(tr("Volume"),100).toInt();
	m_Percent2DScreen = settings.value(tr("Percent2DScreen"),50).toInt();
	m_ActiveHostAdrIndex = settings.value(tr("ActiveHostAdrIndex"),0).toInt();

	m_NCOSpurOffsetI = settings.value(tr("NCOSpurOffsetI"),0.0).toDouble();
	m_NCOSpurOffsetQ = settings.value(tr("NCOSpurOffsetQ"),0.0).toDouble();

	m_TxSignalPower = settings.value(tr("TxSignalPower"),0.0).toDouble();
	m_TxNoisePower = settings.value(tr("TxNoisePower"),-160.0).toDouble();
	m_TxSweepStartFrequency = settings.value(tr("TxSweepStartFrequency"),-1000.0).toInt();
	m_TxSweepStopFrequency = settings.value(tr("TxSweepStopFrequency"),1000.0).toInt();
	m_TxSweepRate = settings.value(tr("TxSweepRate"),0.0).toInt();

	m_UseTestBench = settings.value(tr("UseTestBench"), false).toBool();
	m_AlwaysOnTop = settings.value(tr("AlwaysOnTop"), false).toBool();

	m_InvertSpectrum = settings.value(tr("InvertSpectrum"), false).toBool();
	m_USFm = settings.value(tr("USFm"), true).toBool();
	m_UseCursorText = settings.value(tr("UseCursorText"), false).toBool();

	m_UseUdpFwd = settings.value(tr("UseUdpFwd"), false).toBool();

	m_NoiseProcSettings.NBOn = settings.value(tr("NBOn"), false).toBool();
	m_TxRepeat = settings.value(tr("TxRepeat"), false).toBool();
	m_UseTxFile = settings.value(tr("UseTxFile"), true).toBool();

	m_NoiseProcSettings.NBThreshold = settings.value(tr("NBThreshold"),0).toInt();
	m_NoiseProcSettings.NBWidth = settings.value(tr("NBWidth"),50).toInt();

	m_DemodMode = settings.value(tr("DemodMode"), DEMOD_AM).toInt();
	m_RecordMode = settings.value(tr("RecordMode"), 0).toInt();
	m_DemodFrequency = (qint64)settings.value(tr("DemodFrequency"), 15000000).toUInt();
	m_RecordFilePath = settings.value("RecordFilePath",QCoreApplication::applicationDirPath()+"/Record.wav").toString();
	m_TxFilePath = settings.value("TxFilePath",QCoreApplication::applicationDirPath()+"/Playback.wav").toString();

	settings.endGroup();

	settings.beginGroup(tr("Testbench"));

	g_pTestBench->m_SweepStartFrequency = settings.value(tr("SweepStartFrequency"),0.0).toDouble();
	g_pTestBench->m_SweepStopFrequency = settings.value(tr("SweepStopFrequency"),1.0).toDouble();
	g_pTestBench->m_SweepRate = settings.value(tr("SweepRate"),0.0).toDouble();
	g_pTestBench->m_DisplayRate = settings.value(tr("DisplayRate"),10).toInt();
	g_pTestBench->m_VertRange = settings.value(tr("VertRange"),10000).toInt();
	g_pTestBench->m_TrigIndex = settings.value(tr("TrigIndex"),0).toInt();
	g_pTestBench->m_TrigLevel = settings.value(tr("TrigLevel"),100).toInt();
	g_pTestBench->m_HorzSpan = settings.value(tr("HorzSpan"),100).toInt();
	g_pTestBench->m_Profile = settings.value(tr("Profile"),0).toInt();
	g_pTestBench->m_TimeDisplay = settings.value(tr("TimeDisplay"),false).toBool();
	g_pTestBench->m_GenOn = settings.value(tr("GenOn"),false).toBool();
	g_pTestBench->m_PeakOn = settings.value(tr("PeakOn"),false).toBool();
	g_pTestBench->m_PulseWidth = settings.value(tr("PulseWidth"),0.0).toDouble();
	g_pTestBench->m_PulsePeriod = settings.value(tr("PulsePeriod"),0.0).toDouble();
	g_pTestBench->m_SignalPower = settings.value(tr("SignalPower"),0.0).toDouble();
	g_pTestBench->m_NoisePower = settings.value(tr("NoisePower"),0.0).toDouble();
	g_pTestBench->m_GenMode = settings.value(tr("GenMode"),0).toInt();

	settings.endGroup();

	settings.beginReadArray(tr("Demod"));
	//get demod settings
	for (int i = 0; i < NUM_DEMODS; i++)
	{
		settings.setArrayIndex(i);
		m_DemodSettings[i].HiCut = settings.value(tr("HiCut"), 5000).toInt();
		m_DemodSettings[i].LowCut = settings.value(tr("LowCut"), -5000).toInt();
		m_DemodSettings[i].FreqClickResolution = settings.value(tr("FreqClickResolution"), m_DemodSettings[i].DefFreqClickResolution).toInt();
		m_DemodSettings[i].Offset = settings.value(tr("Offset"), 0).toInt();
		m_DemodSettings[i].SquelchValue = settings.value(tr("SquelchValue"), -160).toInt();
		m_DemodSettings[i].AgcSlope = settings.value(tr("AgcSlope"), 0).toInt();
		m_DemodSettings[i].AgcThresh = settings.value(tr("AgcThresh"), -100).toInt();
		m_DemodSettings[i].AgcManualGain = settings.value(tr("AgcManualGain"), 30).toInt();
		m_DemodSettings[i].AgcDecay = settings.value(tr("AgcDecay"), 200).toInt();
		m_DemodSettings[i].AgcOn = settings.value(tr("AgcOn"),true).toBool();
		m_DemodSettings[i].AgcHangOn = settings.value(tr("AgcHangOn"),false).toBool();
	}
	settings.endArray();

	if(ismin)
		showMinimized();
}

/////////////////////////////////////////////////////////////////////
// Status Timer event handler
/////////////////////////////////////////////////////////////////////
void MainWindow::OnTimer()
{
	OnStatus(m_Status);
	if(++m_KeepAliveTimer>5)
	{
		m_KeepAliveTimer = 0;
		if( (CSdrInterface::RUNNING == m_Status) || ( CSdrInterface::CONNECTED == m_Status) )
			m_pSdrInterface->KeepAlive();
		if( CSdrInterface::NOT_CONNECTED == m_Status )
			m_pSdrInterface->ConnectToServer(m_IPAdr,m_Port);
	}
	ui->frameMeter->SetdBmLevel( m_pSdrInterface->GetSMeterAve(), false );
	if(DEMOD_WFM == m_DemodMode)	//if in WFM mode manage stereo status display
	{
		bool update = false;
		if(	m_FreqChanged )
		{
			m_FreqChanged = false;
			m_RdsDecode.DecodeReset(m_USFm);
			ui->framePlot->m_RdsCall[0] = 0;
			ui->framePlot->m_RdsText[0] = 0;
			update = true;
		}
		else
		{
			tRDS_GROUPS RdsGroups;
			if( m_pSdrInterface->GetStereoLock(NULL) )	//if Stereo pilot state changed
				update = true;
			if( m_pSdrInterface->GetNextRdsGroupData(&RdsGroups) )	//if new data in RDS queue
			{
				if( 0 != RdsGroups.BlockA)
				{	//if valid data in que then decode it
					m_RdsDecode.DecodeRdsGroup(&RdsGroups);
					if( m_RdsDecode.GetRdsString(ui->framePlot->m_RdsText) )
						update = true;
					if( m_RdsDecode.GetRdsCallString(ui->framePlot->m_RdsCall) )
						update = true;
				}
				else
				{	//a zero data block means loss of signal so clear display and reset decoder
					m_RdsDecode.DecodeReset(m_USFm);
					ui->framePlot->m_RdsCall[0] = 0;
					ui->framePlot->m_RdsText[0] = 0;
					update = true;
				}
			}
		}
		if(update)
			ui->framePlot->UpdateOverlay();
	}
}

/////////////////////////////////////////////////////////////////////
// Mouse Right button event handler brings up Demod dialog
/////////////////////////////////////////////////////////////////////
void MainWindow::mousePressEvent(QMouseEvent *event)
{
	if(Qt::RightButton==event->button() )
	{
		OnDemodDlg();
	}
}

/////////////////////////////////////////////////////////////////////
// About Dialog Menu Bar action item handler.
/////////////////////////////////////////////////////////////////////
void MainWindow::OnAbout()
{
CAboutDlg dlg(this,PROGRAM_TITLE_VERSION);
	dlg.exec();
}

/////////////////////////////////////////////////////////////////////
// Menu Bar action item handler.
//Exit menu
/////////////////////////////////////////////////////////////////////
void MainWindow::OnExit()
{
	writeSettings();
	if(m_pSdrInterface)
		m_pSdrInterface->StopIO();
	qApp->exit(0);
}

/////////////////////////////////////////////////////////////////////
// Menu Bar action item handler.
//Display Setup Menu
/////////////////////////////////////////////////////////////////////
void MainWindow::OnDisplayDlg()
{
CDisplayDlg dlg(this);
	dlg.m_FftSize = m_FftSize;
	dlg.m_FftAve = m_FftAve;
	dlg.m_ClickResolution = m_DemodSettings[m_DemodMode].FreqClickResolution;
	dlg.m_MaxDisplayRate = m_MaxDisplayRate;
	dlg.m_UseTestBench = m_UseTestBench;
	dlg.m_Percent2DScreen = m_Percent2DScreen;
	dlg.m_UseCursorText = m_UseCursorText;
	dlg.InitDlg();
	if(QDialog::Accepted == dlg.exec() )
	{
		if(dlg.m_NeedToStop)
		{
			if(CSdrInterface::RUNNING == m_Status)
			{
				m_pSdrInterface->StopSdr();
				ui->framePlot->SetRunningState(false);
			}
		}
		if(m_Percent2DScreen != dlg.m_Percent2DScreen)
		{	//if 2D screen size changed then update it
			m_Percent2DScreen = dlg.m_Percent2DScreen;
			ui->framePlot->SetPercent2DScreen(m_Percent2DScreen);
		}
		m_FftSize = dlg.m_FftSize;
		m_FftAve = dlg.m_FftAve;
		m_UseCursorText = dlg.m_UseCursorText;
		m_DemodSettings[m_DemodMode].FreqClickResolution = dlg.m_ClickResolution;
		m_MaxDisplayRate = dlg.m_MaxDisplayRate;
		m_UseTestBench = dlg.m_UseTestBench;
		m_pSdrInterface->SetFftAve( m_FftAve);
		m_pSdrInterface->SetFftSize( m_FftSize);
		m_pSdrInterface->SetMaxDisplayRate(m_MaxDisplayRate);
		ui->framePlot->SetClickResolution(m_DemodSettings[m_DemodMode].FreqClickResolution);
		ui->framePlot->EnableCurText(m_UseCursorText);
		if(m_UseTestBench)
		{	//make TestBench visable if not already
			if(!g_pTestBench->isVisible())
			{
				//make sure top of dialog is visable(0,0 doesn't include menu bar.Qt bug?)
				if(m_TestBenchRect.top()<30)
					m_TestBenchRect.setTop(30);
				g_pTestBench->setGeometry(m_TestBenchRect);
				g_pTestBench->show();
				g_pTestBench->Init();
			}
			g_pTestBench->activateWindow();
		}
		else
		{	//hide testbench
			if(g_pTestBench->isVisible())
				g_pTestBench->hide();
		}
	}
}

/////////////////////////////////////////////////////////////////////
// Menu Bar action item handler.
//Sound Card Setup Menu
/////////////////////////////////////////////////////////////////////
void MainWindow::OnSoundCardDlg()
{
CSoundDlg dlg(this);
	dlg.SetInputIndex(m_SoundInIndex);
	dlg.SetOutputIndex(m_SoundOutIndex);
	dlg.SetStereo(m_StereoOut);
	if(QDialog::Accepted == dlg.exec() )
	{
		if(CSdrInterface::RUNNING == m_Status)
		{
			m_pSdrInterface->StopSdr();
			ui->framePlot->SetRunningState(false);
		}
		m_StereoOut = dlg.GetStereo();
		m_SoundInIndex = dlg.GetInputIndex();
		m_SoundOutIndex = dlg.GetOutputIndex();
		m_pSdrInterface->SetSoundCardSelection(m_SoundInIndex, m_SoundOutIndex, m_StereoOut);
	}
}

/////////////////////////////////////////////////////////////////////
// Menu Bar action item handler.
//SDR Setup Menu
/////////////////////////////////////////////////////////////////////
void MainWindow::OnSdrDlg()
{
CSdrSetupDlg dlg(this,m_pSdrInterface);
	dlg.m_BandwidthIndex = m_BandwidthIndex;
	dlg.m_USFm = m_USFm;

	dlg.InitDlg();
	dlg.SetSpectrumInversion(m_InvertSpectrum);
	if( dlg.exec() )
	{
		if( m_BandwidthIndex != dlg.m_BandwidthIndex )
		{
			m_BandwidthIndex = dlg.m_BandwidthIndex;
			if(CSdrInterface::RUNNING == m_Status)
			{
				m_pSdrInterface->StopSdr();
				ui->framePlot->SetRunningState(false);
			}
		}
		SetupNoiseProc();
		m_RfGain = dlg.m_RfGain;
		m_USFm = dlg.m_USFm;
		m_pSdrInterface->SetSdrRfGain( dlg.m_RfGain);
		m_pSdrInterface->SetUSFmVersion(m_USFm);
		m_pSdrInterface->SetSdrBandwidthIndex(m_BandwidthIndex);
		quint32 maxspan = m_pSdrInterface->GetMaxBWFromIndex(m_BandwidthIndex);
		ui->SpanspinBox->setMaximum(maxspan/1000);
		if(m_SpanFrequency>maxspan)
			m_SpanFrequency = maxspan;
		ui->SpanspinBox->setValue(m_SpanFrequency/1000);
		ui->framePlot->SetSpanFreq( m_SpanFrequency );
		//limit demod frequency to Center Frequency +/-span frequency
		ui->frameDemodFreqCtrl->Setup(10, m_CenterFrequency-m_SpanFrequency/2,
									  m_CenterFrequency+m_SpanFrequency/2,
									  1,
									  UNITS_KHZ );
		ui->frameDemodFreqCtrl->SetFrequency(m_DemodFrequency);
		m_pSdrInterface->SetDemod(m_DemodMode, m_DemodSettings[m_DemodMode]);

		m_InvertSpectrum = dlg.GetSpectrumInversion();
		m_pSdrInterface->SetSpectrumInversion(m_InvertSpectrum);
	}
}

/////////////////////////////////////////////////////////////////////
// Menu Bar action item handler.
//Network Setup Menu
/////////////////////////////////////////////////////////////////////
void MainWindow::OnNetworkDlg()
{
CEditNetDlg dlg(this);
	dlg.m_IPAdr = m_IPAdr;
	dlg.m_Port = m_Port;
	dlg.m_IPFwdAdr = m_IPFwdAdr;
	dlg.m_FwdPort = m_FwdPort;
	dlg.m_ActiveDevice = m_ActiveDevice;
	dlg.m_UseUdpFwd = m_UseUdpFwd;
	dlg.m_ActiveHostAdrIndex = m_ActiveHostAdrIndex;
	dlg.InitDlg();
	if( dlg.exec() )
	{
		if(	dlg.m_DirtyFlag )
		{
			if(CSdrInterface::RUNNING == m_Status)
			{
				m_pSdrInterface->StopSdr();
				ui->framePlot->SetRunningState(false);
			}
			m_pSdrInterface->StopIO();
			m_IPAdr = dlg.m_IPAdr;
			m_Port = dlg.m_Port;
			m_IPFwdAdr = dlg.m_IPFwdAdr;
			m_FwdPort = dlg.m_FwdPort;
			m_UseUdpFwd = dlg.m_UseUdpFwd;
			m_ActiveDevice = dlg.m_ActiveDevice;
			m_ActiveHostAdrIndex = dlg.m_ActiveHostAdrIndex;
		}
	}
}

/////////////////////////////////////////////////////////////////////
// Menu Bar action item handler.
//Demod Setup Menu (Non-Modal ie allows user to continue in other windows)
/////////////////////////////////////////////////////////////////////
void MainWindow::OnDemodDlg()
{
	m_pDemodSetupDlg->m_DemodMode = m_DemodMode;
	m_pDemodSetupDlg->InitDlg();
	m_pDemodSetupDlg->show();
}

/////////////////////////////////////////////////////////////////////
// Menu Bar action item handler.
//Noise Processing Setup Menu
/////////////////////////////////////////////////////////////////////
void MainWindow::OnNoiseProcDlg()
{
CNoiseProcDlg dlg(this);
	dlg.InitDlg(&m_NoiseProcSettings);
	dlg.exec();
}

/////////////////////////////////////////////////////////////////////
// Menu Bar action item handler.
//TX File data Menu
/////////////////////////////////////////////////////////////////////
void MainWindow::OnFileSendDlg()
{
	CFileTxDlg dlg(this, m_pSdrInterface);
	dlg.m_TxFilePath = m_TxFilePath;
	dlg.m_TxFrequency = m_TxFrequency;
	dlg.m_TxRepeat = m_TxRepeat;
	dlg.m_UseTxFile = m_UseTxFile;
	dlg.m_TxSignalPower = m_TxSignalPower;
	dlg.m_TxNoisePower = m_TxNoisePower;
	dlg.m_TxSweepStartFrequency = m_TxSweepStartFrequency;
	dlg.m_TxSweepStopFrequency = m_TxSweepStopFrequency;
	dlg.m_TxSweepRate = m_TxSweepRate;
	dlg.Init();
	if( dlg.exec() )
	{
		m_TxFilePath = dlg.m_TxFilePath;
		m_TxFrequency = dlg.m_TxFrequency;
		m_TxRepeat = dlg.m_TxRepeat;
		m_UseTxFile = dlg.m_UseTxFile;
		m_TxSignalPower = dlg.m_TxSignalPower;
		m_TxNoisePower = dlg.m_TxNoisePower;
		m_TxSweepStartFrequency = dlg.m_TxSweepStartFrequency;
		m_TxSweepStopFrequency = dlg.m_TxSweepStopFrequency;
		m_TxSweepRate = dlg.m_TxSweepRate;
	}
}

/////////////////////////////////////////////////////////////////////
// Called by Record Setup Menu event
/////////////////////////////////////////////////////////////////////
void MainWindow::OnRecordSetupDlg()
{
CRecordSetupDlg dlg(this);
	dlg.m_RecordMode = m_RecordMode;
	dlg.m_RecordFilePath = m_RecordFilePath;
	dlg.Init();
	if( dlg.exec() )
	{
		m_RecordFilePath = dlg.m_RecordFilePath;
		if(m_RecordMode != dlg.m_RecordMode)
		{
			if(CSdrInterface::RUNNING == m_Status)
			{
				m_pSdrInterface->StopSdr();
				ui->framePlot->SetRunningState(false);
			}
			m_RecordMode = dlg.m_RecordMode;
		}
	}
}

/////////////////////////////////////////////////////////////////////
// Called by Record Button event
/////////////////////////////////////////////////////////////////////
void MainWindow::OnRecord()
{
	if(m_Recording)
	{
		StopRecord();
	}
	else
	{
		if(m_pSdrInterface->StartFileRecord(m_RecordFilePath, m_RecordMode, m_CenterFrequency) )
		{
			m_Recording = true;
			ui->pushButtonRecord->setText("Stop Record");
			ui->pushButtonRecord->setStyleSheet("background-color: rgb(255, 0, 0);");
		}
	}
}
/////////////////////////////////////////////////////////////////////
// Called to stop file recording
/////////////////////////////////////////////////////////////////////
void MainWindow::StopRecord()
{
	m_pSdrInterface->StopFileRecord();
	m_Recording = false;
	ui->pushButtonRecord->setText("Start Record");
	ui->pushButtonRecord->setStyleSheet("background-color: rgb(180, 180, 180);");
}

/////////////////////////////////////////////////////////////////////
// Called to update the information text box
/////////////////////////////////////////////////////////////////////
void MainWindow::UpdateInfoBox()
{
	//display filter cutoffs
	m_Str = QString("%1 %2 %3 %4 %5")
			.arg(m_DemodSettings[m_DemodMode].txt)
			.arg("Lo=").arg(m_DemodSettings[m_DemodMode].LowCut)
			.arg("Hi=").arg(m_DemodSettings[m_DemodMode].HiCut);
	ui->InfoText->setText(m_Str);
}

/////////////////////////////////////////////////////////////////////
// Called by Start/Stop Button event
/////////////////////////////////////////////////////////////////////
void MainWindow::OnRun()
{
	if( CSdrInterface::CONNECTED == m_Status)
	{
		m_CenterFrequency = m_pSdrInterface->SetRxFreq(m_CenterFrequency);
		m_pSdrInterface->SetDemodFreq(m_CenterFrequency - m_DemodFrequency);
		m_pSdrInterface->SetForwardingParameters( m_UseUdpFwd, m_IPFwdAdr, m_FwdPort);
		m_pSdrInterface->StartSdr();
		m_pSdrInterface->m_MissedPackets = 0;

		ui->framePlot->SetRunningState(true);
		InitPerformance();
		m_RdsDecode.DecodeReset(m_USFm);
	}
	else if(CSdrInterface::RUNNING == m_Status)
	{
		StopRecord();
		m_pSdrInterface->StopSdr();
		ui->framePlot->SetRunningState(false);
		ReadPerformance();
	}
}

/////////////////////////////////////////////////////////////////////
// Event: New FFT Display Data is available so draw it
// Called when new spectrum data is available to be displayed
/////////////////////////////////////////////////////////////////////
void MainWindow::OnNewFftData()
{
	if( CSdrInterface::RUNNING == m_Status)
		ui->framePlot->draw();
}

/////////////////////////////////////////////////////////////////////
// Manage Status states. Called periodically by Timer event
/////////////////////////////////////////////////////////////////////
void MainWindow::OnStatus(int status)
{
	m_Status = (CSdrInterface::eStatus)status;
//qDebug()<<"Status"<< m_Status;
	switch(status)
	{
		case CSdrInterface::NOT_CONNECTED:
		case CSdrInterface::CONNECTING:
			if(	m_LastStatus == CSdrInterface::RUNNING)
			{
				m_pSdrInterface->StopSdr();
				ui->framePlot->SetRunningState(false);
			}
			ui->statusBar->showMessage(tr("SDR Not Connected"), 0);
			ui->pushButtonRun->setText(tr("Run"));
			ui->pushButtonRun->setEnabled(false);
			break;
		case CSdrInterface::CONNECTED:
			if(	m_LastStatus == CSdrInterface::RUNNING)
			{
				m_pSdrInterface->StopSdr();
				ui->framePlot->SetRunningState(false);
			}
			ui->statusBar->showMessage( m_ActiveDevice + tr(" Connected"), 0);
			if(	(m_LastStatus == CSdrInterface::NOT_CONNECTED) ||
				(m_LastStatus == CSdrInterface::CONNECTING) )
					m_pSdrInterface->GetSdrInfo();
			ui->pushButtonRun->setText(tr("Run"));
			ui->pushButtonRun->setEnabled(true);
			break;
		case CSdrInterface::RUNNING:
			m_Str.setNum(m_pSdrInterface->GetRateError());
			m_Str.append(tr(" ppm  Missed Pkts="));
			m_Str2.setNum(m_pSdrInterface->m_MissedPackets);
			m_Str.append(m_Str2);
			ui->statusBar->showMessage(m_ActiveDevice + tr(" Running   ") + m_Str, 0);
			ui->pushButtonRun->setText(tr("Stop"));
			ui->pushButtonRun->setEnabled(true);
			break;
		case CSdrInterface::ERR:
			if(	m_LastStatus == CSdrInterface::RUNNING)
			{
				m_pSdrInterface->StopSdr();
				ui->framePlot->SetRunningState(false);
			}
			ui->statusBar->showMessage(tr("SDR Not Connected"), 0);
			ui->pushButtonRun->setText(tr("Run"));
			ui->pushButtonRun->setEnabled(false);
			break;
		case CSdrInterface::ADOVR:
			if(	m_LastStatus == CSdrInterface::RUNNING)
			{
				m_Status = CSdrInterface::RUNNING;
				ui->framePlot->SetADOverload(true);
			}
			break;
		default:
			break;
	}
	m_LastStatus = m_Status;
}

/////////////////////////////////////////////////////////////////////
// Event handler that New SDR Info is available
// Called when a radio is first conencted and it reports its information
/////////////////////////////////////////////////////////////////////
void MainWindow::OnNewInfoData()
{
	m_ActiveDevice = m_pSdrInterface->m_DeviceName;
	m_RadioType = m_pSdrInterface->GetRadioType();
	m_pSdrInterface->SetSdrBandwidthIndex(m_BandwidthIndex);

	//update span limits to new radio attached
	quint32 maxspan = m_pSdrInterface->GetMaxBWFromIndex(m_BandwidthIndex);
	ui->SpanspinBox->setMaximum(maxspan/1000);
	if(m_SpanFrequency>maxspan)
		m_SpanFrequency = maxspan;
	ui->SpanspinBox->setValue(m_SpanFrequency/1000);
	m_LastSpanKhz = m_SpanFrequency/1000;
	ui->framePlot->SetSpanFreq( m_SpanFrequency );
	m_pSdrInterface->SetDemod(m_DemodMode, m_DemodSettings[m_DemodMode]);
}

/////////////////////////////////////////////////////////////////////
// Handle change event for center frequency control
/////////////////////////////////////////////////////////////////////
void MainWindow::OnNewCenterFrequency(qint64 freq)
{
//qDebug()<<"F = "<<freq;
	m_CenterFrequency = m_pSdrInterface->SetRxFreq(freq);
	if(m_CenterFrequency!=freq)	//if freq was clamped by sdr range then update control again
		ui->frameFreqCtrl->SetFrequency(m_CenterFrequency);
	m_DemodFrequency = m_CenterFrequency;
	m_pSdrInterface->SetDemodFreq(m_CenterFrequency - m_DemodFrequency);
	ui->framePlot->SetCenterFreq( m_CenterFrequency );
	ui->framePlot->SetDemodCenterFreq( m_DemodFrequency );
	//limit demod frequency to Center Frequency +/-span frequency
	ui->frameDemodFreqCtrl->Setup(10, m_CenterFrequency-m_SpanFrequency/2,
								  m_CenterFrequency+m_SpanFrequency/2,
								  1,
								  UNITS_KHZ );
	ui->frameDemodFreqCtrl->SetFrequency(m_DemodFrequency);
	m_FreqChanged = true;
	ui->framePlot->UpdateOverlay();
}

/////////////////////////////////////////////////////////////////////
// Handle change event for demod frequency control
/////////////////////////////////////////////////////////////////////
void MainWindow::OnNewDemodFrequency(qint64 freq)
{
	m_DemodFrequency = freq;
	ui->framePlot->SetDemodCenterFreq( m_DemodFrequency );
	ui->framePlot->UpdateOverlay();
	m_pSdrInterface->SetDemodFreq(m_CenterFrequency - m_DemodFrequency);
	m_FreqChanged = true;
}

/////////////////////////////////////////////////////////////////////
// Handle plot mouse change event for center frequency
/////////////////////////////////////////////////////////////////////
void MainWindow::OnNewScreenCenterFreq(qint64 freq)
{
	m_CenterFrequency = freq;
	ui->frameFreqCtrl->SetFrequency(m_CenterFrequency);
}


/////////////////////////////////////////////////////////////////////
// Handle plot mouse change event for demod frequency
/////////////////////////////////////////////////////////////////////
void MainWindow::OnNewScreenDemodFreq(qint64 freq)
{
	m_DemodFrequency = freq;
	ui->frameDemodFreqCtrl->SetFrequency(m_DemodFrequency);
}

/////////////////////////////////////////////////////////////////////
// Handle plot mouse change event for filter cutoff frequencies
/////////////////////////////////////////////////////////////////////
void MainWindow::OnNewLowCutFreq(int freq)
{
	m_DemodSettings[m_DemodMode].LowCut = freq;
	UpdateInfoBox();
	m_pSdrInterface->SetDemod(m_DemodMode, m_DemodSettings[m_DemodMode]);
}

void MainWindow::OnNewHighCutFreq(int freq)
{
	m_DemodSettings[m_DemodMode].HiCut = freq;
	UpdateInfoBox();
	m_pSdrInterface->SetDemod(m_DemodMode, m_DemodSettings[m_DemodMode]);
}


/////////////////////////////////////////////////////////////////////
// Handle Span Spin Control change event
/////////////////////////////////////////////////////////////////////
void MainWindow::OnSpanChanged(int spanKhz)
{
	if( (spanKhz>m_LastSpanKhz) && (spanKhz==10))
	{//if going higher and is 10KHz
		//change stepsize to 10KHz
		ui->SpanspinBox->setSingleStep(10);
	}
	else if( (spanKhz<m_LastSpanKhz) && (spanKhz==10))
	{	//if going lower and is 10KHz
		//change stepsize to 1KHz
		ui->SpanspinBox->setSingleStep(1);
}

	m_LastSpanKhz = spanKhz;
	m_SpanFrequency = spanKhz*1000;
	ui->framePlot->SetSpanFreq(m_SpanFrequency);
	ui->framePlot->UpdateOverlay();
	//limit demod frequency to Center Frequency +/-span frequency
	ui->frameDemodFreqCtrl->Setup(10, m_CenterFrequency-m_SpanFrequency/2,
								  m_CenterFrequency+m_SpanFrequency/2,
								  1,
								  UNITS_KHZ );
	ui->frameDemodFreqCtrl->SetFrequency(m_DemodFrequency);

}

/////////////////////////////////////////////////////////////////////
// Handle Max dB Spin Control change event
/////////////////////////////////////////////////////////////////////
void MainWindow::OnMaxdBChanged(int maxdb)
{
	m_MaxdB = maxdb;
	ui->framePlot->SetMaxdB(m_MaxdB);
	ui->framePlot->UpdateOverlay();
}

/////////////////////////////////////////////////////////////////////
// Handle Vertical scale COmbo box Control change event
/////////////////////////////////////////////////////////////////////
void MainWindow::OnVertScaleChanged(int index)
{
	//hack to ignor event when control is initialized
	if(ui->ScalecomboBox->count() != 4)
		return;
	m_VertScaleIndex = index;
	int LastdBStepsize = m_dBStepSize;
	int LastMaxdB = m_MaxdB;
	m_dBStepSize = (int)ui->ScalecomboBox->itemData(m_VertScaleIndex).toInt();
	ui->framePlot->SetdBStepSize(m_dBStepSize);
	ui->MaxdBspinBox->setSingleStep(m_dBStepSize);
	ui->MaxdBspinBox->setMinimum(MIN_FFTDB+VERT_DIVS*m_dBStepSize);
	ui->MaxdBspinBox->setMaximum(MAX_FFTDB);

	//adjust m_MaxdB to try and keep signal roughly centered at bottom of screen
	if(m_dBStepSize!=LastdBStepsize)
	{
		m_MaxdB = LastMaxdB + 11*(m_dBStepSize-LastdBStepsize);
		m_MaxdB = (m_MaxdB/m_dBStepSize)*m_dBStepSize;
	}

	ui->MaxdBspinBox->setValue(m_MaxdB);
	ui->framePlot->SetMaxdB(m_MaxdB);
	ui->framePlot->UpdateOverlay();

//qDebug()<<"m_VertScaleIndex="<<m_VertScaleIndex;
//qDebug()<<"dBstep="<<m_dBStepSize;
}

void MainWindow::OnVolumeSlider(int value)
{
	m_Volume = value;
	m_pSdrInterface->SetVolume(m_Volume);
}

/////////////////////////////////////////////////////////////////////
// Setup Noise Processor parameters from 'm_NoiseProcSettings'
/////////////////////////////////////////////////////////////////////
void MainWindow::SetupNoiseProc()
{
	m_pSdrInterface->SetupNoiseProc(&m_NoiseProcSettings);
}

/////////////////////////////////////////////////////////////////////
// Called when memory dialog checkbox is clicked
/////////////////////////////////////////////////////////////////////
void MainWindow::SetChatDialogState(int state)
{
	if(state)
	{	//make memory dialog visable if not already
		if(!g_pChatDialog->isVisible())
		{
			g_pChatDialog->setGeometry(m_ChatDialogRect);
			g_pChatDialog->show();
		}
		g_pChatDialog->activateWindow();
		g_pChatDialog->raise();
	}
	else
	{	//hide memory dialog
		if(g_pChatDialog->isVisible())
		{
			m_ChatDialogRect = g_pChatDialog->geometry();
			g_pChatDialog->hide();
		}
	}
}

/////////////////////////////////////////////////////////////////////
// Setup Demod parameters and clamp to limits
/////////////////////////////////////////////////////////////////////
void MainWindow::SetupDemod(int index)
{
qDebug()<<index;
	if(m_DemodMode != index)
	{
		if( (DEMOD_PSK == index) || (DEMOD_FSK == index) )
			SetChatDialogState(true);
		else
			SetChatDialogState(false);
	}
	m_DemodMode = index;
	ui->framePlot->SetDemodRanges(
			m_DemodSettings[m_DemodMode].LowCutmin,
			m_DemodSettings[m_DemodMode].LowCutmax,
			m_DemodSettings[m_DemodMode].HiCutmin,
			m_DemodSettings[m_DemodMode].HiCutmax,
			m_DemodSettings[m_DemodMode].Symetric);
	//Clamp cutoff freqs to range of particular demod
	if(m_DemodSettings[index].LowCut < m_DemodSettings[index].LowCutmin)
		m_DemodSettings[index].LowCut = m_DemodSettings[index].LowCutmin;
	if(m_DemodSettings[index].LowCut > m_DemodSettings[index].LowCutmax)
		m_DemodSettings[index].LowCut = m_DemodSettings[index].LowCutmax;

	if(m_DemodSettings[index].HiCut < m_DemodSettings[index].HiCutmin)
		m_DemodSettings[index].HiCut = m_DemodSettings[index].HiCutmin;
	if(m_DemodSettings[index].HiCut > m_DemodSettings[index].HiCutmax)
		m_DemodSettings[index].HiCut = m_DemodSettings[index].HiCutmax;

	ui->framePlot->SetHiLowCutFrequencies(m_DemodSettings[m_DemodMode].LowCut,
										  m_DemodSettings[m_DemodMode].HiCut);
	ui->framePlot->SetFilterClickResolution(m_DemodSettings[m_DemodMode].FilterClickResolution);
	ui->framePlot->UpdateOverlay();
	m_pSdrInterface->SetDemod(m_DemodMode, m_DemodSettings[m_DemodMode]);
	m_pSdrInterface->SetDemodFreq(m_CenterFrequency - m_DemodFrequency);
	UpdateInfoBox();
	ui->frameMeter->SetSquelchPos( m_DemodSettings[m_DemodMode].SquelchValue );
	ui->framePlot->SetClickResolution(m_DemodSettings[m_DemodMode].FreqClickResolution);
}


/////////////////////////////////////////////////////////////////////
// Setup Demod initial parameters/limits
/////////////////////////////////////////////////////////////////////
void MainWindow::InitDemodSettings()
{
	//set filter limits based on final sample rates etc.
	//These parameters are fixed and not saved in Settings
	m_DemodSettings[DEMOD_AM].txt = tr("AM");
	m_DemodSettings[DEMOD_AM].HiCutmin = 500;
	m_DemodSettings[DEMOD_AM].HiCutmax = 10000;
	m_DemodSettings[DEMOD_AM].LowCutmax = -500;
	m_DemodSettings[DEMOD_AM].LowCutmin = -10000;
	m_DemodSettings[DEMOD_AM].Symetric = true;
	m_DemodSettings[DEMOD_AM].DefFreqClickResolution = 1000;
	m_DemodSettings[DEMOD_AM].FilterClickResolution = 100;

	m_DemodSettings[DEMOD_SAM].txt = tr("AM");
	m_DemodSettings[DEMOD_SAM].HiCutmin = 100;
	m_DemodSettings[DEMOD_SAM].HiCutmax = 10000;
	m_DemodSettings[DEMOD_SAM].LowCutmax = -100;
	m_DemodSettings[DEMOD_SAM].LowCutmin = -10000;
	m_DemodSettings[DEMOD_SAM].Symetric = false;
	m_DemodSettings[DEMOD_SAM].DefFreqClickResolution = 1000;
	m_DemodSettings[DEMOD_SAM].FilterClickResolution = 100;

	m_DemodSettings[DEMOD_FM].txt = tr("FM");
	m_DemodSettings[DEMOD_FM].HiCutmin = 5000;
	m_DemodSettings[DEMOD_FM].HiCutmax = 15000;
	m_DemodSettings[DEMOD_FM].LowCutmax = -5000;
	m_DemodSettings[DEMOD_FM].LowCutmin = -15000;
	m_DemodSettings[DEMOD_FM].Symetric = true;
	m_DemodSettings[DEMOD_FM].DefFreqClickResolution = 5000;
	m_DemodSettings[DEMOD_FM].FilterClickResolution = 5000;

	m_DemodSettings[DEMOD_WFM].txt = tr("WFM");
	m_DemodSettings[DEMOD_WFM].HiCutmin = 100000;
	m_DemodSettings[DEMOD_WFM].HiCutmax = 100000;
	m_DemodSettings[DEMOD_WFM].LowCutmax = -100000;
	m_DemodSettings[DEMOD_WFM].LowCutmin = -100000;
	m_DemodSettings[DEMOD_WFM].Symetric = true;
	m_DemodSettings[DEMOD_WFM].DefFreqClickResolution = 100000;
	m_DemodSettings[DEMOD_WFM].FilterClickResolution = 10000;

	m_DemodSettings[DEMOD_USB].txt = tr("USB");
	m_DemodSettings[DEMOD_USB].HiCutmin = 500;
	m_DemodSettings[DEMOD_USB].HiCutmax = 20000;
	m_DemodSettings[DEMOD_USB].LowCutmax = 200;
	m_DemodSettings[DEMOD_USB].LowCutmin = 0;
	m_DemodSettings[DEMOD_USB].Symetric = false;
	m_DemodSettings[DEMOD_USB].DefFreqClickResolution = 100;
	m_DemodSettings[DEMOD_USB].FilterClickResolution = 100;

	m_DemodSettings[DEMOD_LSB].txt = tr("LSB");
	m_DemodSettings[DEMOD_LSB].HiCutmin = -200;
	m_DemodSettings[DEMOD_LSB].HiCutmax = 0;
	m_DemodSettings[DEMOD_LSB].LowCutmax = -500;
	m_DemodSettings[DEMOD_LSB].LowCutmin = -20000;
	m_DemodSettings[DEMOD_LSB].Symetric = false;
	m_DemodSettings[DEMOD_LSB].DefFreqClickResolution = 100;
	m_DemodSettings[DEMOD_LSB].FilterClickResolution = 100;

	m_DemodSettings[DEMOD_CWU].txt = tr("CWU");
	m_DemodSettings[DEMOD_CWU].HiCutmin = 50;
	m_DemodSettings[DEMOD_CWU].HiCutmax = 1000;
	m_DemodSettings[DEMOD_CWU].LowCutmax = -50;
	m_DemodSettings[DEMOD_CWU].LowCutmin = -1000;
	m_DemodSettings[DEMOD_CWU].Symetric = false;
	m_DemodSettings[DEMOD_CWU].DefFreqClickResolution = 10;
	m_DemodSettings[DEMOD_CWU].FilterClickResolution = 50;

	m_DemodSettings[DEMOD_CWL].txt = tr("CWL");
	m_DemodSettings[DEMOD_CWL].HiCutmin = 50;
	m_DemodSettings[DEMOD_CWL].HiCutmax = 1000;
	m_DemodSettings[DEMOD_CWL].LowCutmax = -50;
	m_DemodSettings[DEMOD_CWL].LowCutmin = -1000;
	m_DemodSettings[DEMOD_CWL].Symetric = false;
	m_DemodSettings[DEMOD_CWL].DefFreqClickResolution = 10;
	m_DemodSettings[DEMOD_CWL].FilterClickResolution = 50;

	m_DemodSettings[DEMOD_PSK].txt = tr("PSK");
	m_DemodSettings[DEMOD_PSK].HiCutmin = 50;
	m_DemodSettings[DEMOD_PSK].HiCutmax = 50;
	m_DemodSettings[DEMOD_PSK].LowCutmax = -50;
	m_DemodSettings[DEMOD_PSK].LowCutmin = -50;
	m_DemodSettings[DEMOD_PSK].Symetric = true;
	m_DemodSettings[DEMOD_PSK].DefFreqClickResolution = 1;
	m_DemodSettings[DEMOD_PSK].FilterClickResolution = 5;

	m_DemodSettings[DEMOD_FSK].txt = tr("Raw DSC");
	m_DemodSettings[DEMOD_FSK].HiCutmin = 20;
	m_DemodSettings[DEMOD_FSK].HiCutmax = 200;
	m_DemodSettings[DEMOD_FSK].LowCutmax = -20;
	m_DemodSettings[DEMOD_FSK].LowCutmin = -200;
	m_DemodSettings[DEMOD_FSK].Symetric = true;
	m_DemodSettings[DEMOD_FSK].DefFreqClickResolution = 10;
	m_DemodSettings[DEMOD_FSK].FilterClickResolution = 10;
}
