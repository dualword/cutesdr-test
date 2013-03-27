//////////////////////////////////////////////////////////////////////
// samdemod.h: interface for the CSamDemod class.
//
// History:
//	2010-09-22  Initial creation MSW
//	2011-03-27  Initial release
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
//=============================================================================
#ifndef SAMDEMOD_H
#define SAMDEMOD_H
#include "dsp/datatypes.h"
#include "dsp/fir.h"

class CSamDemod
{
public:
	CSamDemod(TYPEREAL samplerate);
	//overloaded functions for mono and stereo
	int ProcessData(int InLength, TYPECPX* pInData, TYPEREAL* pOutData);
	int ProcessData(int InLength, TYPECPX* pInData, TYPECPX* pOutData);
private:
	TYPEREAL m_SampleRate;
	TYPEREAL m_z1;
	TYPEREAL m_y1;
	TYPEREAL m_NcoPhase;
	TYPEREAL m_NcoFreq;
	TYPEREAL m_NcoAcc;
	TYPEREAL m_NcoLLimit;
	TYPEREAL m_NcoHLimit;
	TYPEREAL m_PllAlpha;
	TYPEREAL m_PllBeta;
	CFir m_Fir;
};

#endif // SAMDEMOD_H
