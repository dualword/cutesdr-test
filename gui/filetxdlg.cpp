#include "filetxdlg.h"
#include "ui_filetxdlg.h"
#include <QFileDialog>
#include  "interface/wavefilewriter.h"

CFileTxDlg::CFileTxDlg(QWidget *parent, CSdrInterface* pSdrInterface) :
	QDialog(parent),
	m_pSdrInterface(pSdrInterface),
	ui(new Ui::CFileTxDlg)
{
	ui->setupUi(this);
	ui->frameTxFreqCtrl->Setup(10, 100U, 1700000000U, 1, UNITS_MHZ );
	ui->frameTxFreqCtrl->SetBkColor(Qt::black);
	ui->frameTxFreqCtrl->SetDigitColor(Qt::yellow);
	ui->frameTxFreqCtrl->SetUnitsColor(Qt::lightGray);
	ui->frameTxFreqCtrl->SetHighlightColor(Qt::darkGray);
	m_TxFilePath = "";
	m_TxRepeat = false;

}

CFileTxDlg::~CFileTxDlg()
{
	delete ui;
}

void CFileTxDlg::Init()
{
	connect(m_pSdrInterface, SIGNAL(NewTxMsg(int)), this, SLOT(NewTxMsgSlot(int)));
	connect(ui->frameTxFreqCtrl, SIGNAL(NewFrequency(qint64)), this, SLOT(OnNewCenterFrequency(qint64)));
	ui->lineEdit->setText(m_TxFilePath);
	ui->frameTxFreqCtrl->SetFrequency(m_TxFrequency);
	ui->checkBoxRepeat->setChecked(m_TxRepeat);
	m_FileReader.open(m_TxFilePath);
	ui->labelFileInfo->setText(m_FileReader.m_FileInfoStr);
	m_FileReader.close();
}

void CFileTxDlg::done(int r)	//virtual override
{
	SetTxState(false);
	QDialog::done(r);	//call base class
}

/////////////////////////////////////////////////////////////////////
// Handle change event for center frequency control
/////////////////////////////////////////////////////////////////////
void CFileTxDlg::OnNewCenterFrequency(qint64 freq)
{
	m_TxFrequency = freq;
	SetTxFreq(m_TxFrequency);
}

void CFileTxDlg::on_pushButtonFileSelect_clicked()
{
	QString str = QFileDialog::getOpenFileName(this,tr("Select .wav File Base Name"),m_TxFilePath,tr("wav files (*.wav)"));
	if(str != "")
	{
		m_TxFilePath = str;
		ui->lineEdit->setText(m_TxFilePath);
		m_FileReader.open(m_TxFilePath);
		ui->labelFileInfo->setText(m_FileReader.m_FileInfoStr);
		m_FileReader.close();
	}
}

void CFileTxDlg::on_checkBoxRepeat_clicked(bool checked)
{
	m_TxRepeat = checked;
}

void CFileTxDlg::on_pushButtonStart_clicked()
{
	CWaveFileWriter FileWriter;
	CDataModifier DataModifier;

	if(m_FileReader.open(m_TxFilePath) )
	{
		if( !FileWriter.open( "d:\\testwr.wav",true, m_FileReader.GetSampleRate(), true, 0) )
		{
			m_FileReader.close();
			qDebug()<<"File write open error";
			return;
		}
		int sampleswritten = 0;
		int samplesread = 0;
		DataModifier.Init(m_FileReader.GetSampleRate());
		DataModifier.SetSweepRate(1.0);
		DataModifier.SetSweepStart(-100.0);
		DataModifier.SetSweepStop(100.0);
		while(sampleswritten < m_FileReader.GetNumberSamples())
		{
			//copy in blocks of 512 samples
			samplesread = m_FileReader.GetNextDataBlock( m_TxDataBuf, 512);
			if( samplesread > 0 )
			{
				DataModifier.ProcessBlock(m_TxDataBuf, samplesread);
				if( !FileWriter.Write(m_TxDataBuf, samplesread) )
				{
					m_FileReader.close();
					FileWriter.close();
					qDebug()<<"File copy error";
					m_FileReader.close();
					FileWriter.close();
					return;
				}
				sampleswritten += samplesread;
			}
			else
			{
				if(samplesread < 0 )
					qDebug()<<"File read error";
				else
					qDebug()<<"File operation complete";
				break;
			}
		}
		m_FileReader.close();
		FileWriter.close();
	}
	else
	{
		qDebug()<<"File read open Fail";
	}
}

void CFileTxDlg::on_pushButtonStartTest_clicked()
{
	SetTxFreq(m_TxFrequency);
	m_DataModifier.Init(32000);
	m_DataModifier.SetSweepRate(5000.0);
	m_DataModifier.SetSweepStart(-15000.0);
	m_DataModifier.SetSweepStop(15000.0);
	SetTxState(true);
}

void CFileTxDlg::on_pushButtonStopTest_clicked()
{
	SetTxState(false);
}

void CFileTxDlg::NewTxMsgSlot(int FifoPtr)
{
	quint8 chan;
	tBtoS tmp16;
	if( NULL==m_pSdrInterface)
		return;
	CAscpRxMsg* pMsg = &(m_pSdrInterface->m_TxAscpMsg[FifoPtr]);
	pMsg->InitRxMsg();

	if( pMsg->GetType() == TYPE_TARG_RESP_CITEM )
	{	// Is a message from SDR in response to a request
		switch(pMsg->GetCItem())
		{
			case CI_TX_STATE:
				chan = pMsg->GetParm8();
				qDebug()<<"Tx State is = "<<pMsg->GetParm8();
				break;
			case CI_TX_FREQUENCY:	//set transmit center frequency
				chan = pMsg->GetParm8();
				qDebug()<<"Tx Frequency is = "<<pMsg->GetParm32();
				break;
		}
	}
	else if( pMsg->GetType() == TYPE_TARG_DATA_ITEM0 )
	{
	}
	else if( pMsg->GetType() == TYPE_TARG_DATA_ITEM1 )
	{
	}
	else if( pMsg->GetType() == TYPE_TARG_DATA_ITEM2 )
	{
	}
	else if( pMsg->GetType() == TYPE_TARG_DATA_ITEM3 )
	{
	}
	else if(pMsg->GetType() == TYPE_DATA_ITEM_ACK)
	{	//is TX fifo status message from SDR
		tmp16.bytes.b0 = pMsg->Buf8[3];
		tmp16.bytes.b1 = pMsg->Buf8[4];
		int BytesAvail = tmp16.all;
//		qDebug()<<"Tx FIFO = "<<BytesAvail;
		while(BytesAvail >= (240*6) )
		{
			GenerateTestData(m_TestBuf, 240);
			int sent = SendIQDataBlk( m_TestBuf, 240);
			BytesAvail -= sent;
		}
//		qDebug()<<"BA "<<BytesAvail;
	}
}

void CFileTxDlg::GenerateTestData(tICpx32* pBuf, int NumSamples)
{
	for(int i=0; i<NumSamples; i++)
	{
		m_TxDataBuf[i].re = 0.707;
		m_TxDataBuf[i].im = 0.707;
	}
	m_DataModifier.ProcessBlock(m_TxDataBuf, NumSamples);

	for(int i=0; i<NumSamples; i++)
	{
			pBuf[i].re = (qint32)(m_TxDataBuf[i].re * 2.147483648e9);
			pBuf[i].im = (qint32)(m_TxDataBuf[i].im * 2.147483648e9);
	}
}

///////////////////////////////////////////////////////////////////////////////
// Sends TX Center frequency command to SDR
///////////////////////////////////////////////////////////////////////////////
void CFileTxDlg::SetTxFreq(quint32 freq)
{
CAscpTxMsg TxMsg;
	TxMsg.InitTxMsg(TYPE_HOST_SET_CITEM);
	TxMsg.AddCItem(CI_TX_FREQUENCY);
	TxMsg.AddParm8(0);
	TxMsg.AddParm32( freq );
	TxMsg.AddParm8(0);		//5th byte of freq
	if(m_pSdrInterface)
		m_pSdrInterface->SendUdpMsg(TxMsg.Buf8,TxMsg.GetLength() );
}

/////////////////////////////////////////////////////////////////////
// Called to start the Tx
/////////////////////////////////////////////////////////////////////
void CFileTxDlg::SetTxState(bool On)
{
CAscpTxMsg TxMsg;
	m_SeqNumber = 0;
	TxMsg.InitTxMsg(TYPE_HOST_SET_CITEM);
	TxMsg.AddCItem(CI_TX_STATE);
	TxMsg.AddParm8(0);
	if(On)
		TxMsg.AddParm8(TX_STATE_ON);
	else
		TxMsg.AddParm8(TX_STATE_OFF);
	if(m_pSdrInterface)
		m_pSdrInterface->SendUdpMsg(TxMsg.Buf8,TxMsg.GetLength() );
}

int CFileTxDlg::SendIQDataBlk(tICpx32* pData, int NumSamples)
{
	tASCPDataMsg TxMsg;
	int len=0;
	for(int j=0; j<NumSamples; j++)
	{
		TxMsg.fld.DataBuf[len++] = (pData[j].re>>24) & 0xFF;	//I2
		TxMsg.fld.DataBuf[len++] = (pData[j].re>>16) & 0xFF;	//I1
		TxMsg.fld.DataBuf[len++] = (pData[j].re>>8) & 0xFF;		//I0

		TxMsg.fld.DataBuf[len++] = (pData[j].im>>24) & 0xFF;	//Q2
		TxMsg.fld.DataBuf[len++] = (pData[j].im>>16) & 0xFF;	//Q1
		TxMsg.fld.DataBuf[len++] = (pData[j].im>>8) & 0xFF;		//Q0
	}
	TxMsg.fld.Hdr = TYPE_TARG_DATA_ITEM0<<8;
	TxMsg.fld.Hdr += (len + 4);
	TxMsg.fld.Sequence = m_SeqNumber++;
	if(m_pSdrInterface)
		m_pSdrInterface->SendUdpMsg(TxMsg.Buf8, len+4);
	return len;
}


