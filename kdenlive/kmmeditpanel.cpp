/***************************************************************************
                          kmmeditpanelimplementation.cpp  -  description
                             -------------------
    begin                : Mon Apr 8 2002
    copyright            : (C) 2002 by Jason Wood
    email                : jasonwood@blueyonder.co.uk
 ***************************************************************************/

/***************************************************************************
 *                                                                         *
 *   This program is free software; you can redistribute it and/or modify  *
 *   it under the terms of the GNU General Public License as published by  *
 *   the Free Software Foundation; either version 2 of the License, or     *
 *   (at your option) any later version.                                   *
 *                                                                         *
 ***************************************************************************/

#include <cmath>

#include <kled.h>

#include "kmmeditpanel.h"

#include "kiconloader.h"
#include "qtoolbutton.h"

#include "kfixedruler.h"
#include "krulertimemodel.h"
#include "kdenlivedoc.h"

KMMEditPanel::KMMEditPanel(KdenliveDoc *document, QWidget* parent, const char* name, WFlags fl) :
																 KMMEditPanel_UI(parent, name, fl)
{
	m_document = document;

	m_ruler->setRulerModel(new KRulerTimeModel());
	m_ruler->setRange(0, 1500);
	m_ruler->setMargin(40);
  m_ruler->setAutoClickSlider(0);

	m_ruler->addSlider(KRuler::Diamond, 0);
	m_ruler->addSlider(KRuler::StartMark, 0);
	m_ruler->addSlider(KRuler::EndMark, m_ruler->maxValue());
  
  renderStatus->off();
  renderStatus->setColor(QColor(0, 200, 0));
  renderStatus->setFixedSize(20, 20);
  
	KIconLoader loader;  

	startButton->setPixmap(loader.loadIcon("player_start", KIcon::Toolbar));
	rewindButton->setPixmap(loader.loadIcon("player_rew", KIcon::Toolbar));
	stopButton->setPixmap(loader.loadIcon("player_stop", KIcon::Toolbar));
	playButton->setPixmap(loader.loadIcon("player_play", KIcon::Toolbar));
	forwardButton->setPixmap(loader.loadIcon("player_fwd", KIcon::Toolbar));
	endButton->setPixmap(loader.loadIcon("player_end", KIcon::Toolbar));

	connect(m_ruler, SIGNAL(sliderValueChanged(int, int)), this, SLOT(rulerValueChanged(int, int)));

	connect(startButton, SIGNAL(pressed()), this, SLOT(seekBeginning()));
	connect(endButton, SIGNAL(pressed()), this, SLOT(seekEnd()));	
	
	connect(playButton, SIGNAL(pressed()), this, SLOT(play()));
	connect(rewindButton, SIGNAL(pressed()), this, SLOT(stepBack()));
	connect(stopButton, SIGNAL(pressed()), this, SLOT(stop()));
	connect(forwardButton, SIGNAL(pressed()), this, SLOT(stepForwards()));
  connect(inpointButton, SIGNAL(pressed()), this, SLOT(setInpoint()));
  connect(outpointButton, SIGNAL(pressed()), this, SLOT(setOutpoint()));
}

KMMEditPanel::~KMMEditPanel()
{
}

/** Sets the length of the clip that we are viewing. */
void KMMEditPanel::setClipLength(int frames)
{
	m_ruler->setMaxValue(frames);
}

/** A slider on the ruler has changed value */
void KMMEditPanel::rulerValueChanged(int ID, int value)
{
  switch(ID) {
    case 0 :
  		emit seekPositionChanged(GenTime(value, m_document->framesPerSecond()));
      break;
    case 1 :
      emit inpointChanged(GenTime(value, m_document->framesPerSecond()));
      break;
    case 2 :
      emit outpointChanged(GenTime(value, m_document->framesPerSecond()));
      break;
  }
}

/** Seeks to the beginning of the ruler. */
void KMMEditPanel::seekBeginning()
{
	m_ruler->setSliderValue(0, m_ruler->minValue());
}

/** Seeks to the end of the ruler */
void KMMEditPanel::seekEnd()
{
	m_ruler->setSliderValue(0, m_ruler->maxValue());
}

void KMMEditPanel::stepBack(){
	m_ruler->setSliderValue(0, m_ruler->getSliderValue(0)-1);
}

void KMMEditPanel::stepForwards()
{
	m_ruler->setSliderValue(0, m_ruler->getSliderValue(0)+1);
}

void KMMEditPanel::play()
{
	emit playSpeedChanged(1.0);
}

void KMMEditPanel::stop()
{
	emit playSpeedChanged(0.0);
}

void KMMEditPanel::seek(const GenTime &time)
{
  m_ruler->setSliderValue(0, (int)(floor(time.frames(m_document->framesPerSecond()) + 0.5)));
}

void KMMEditPanel::rendererConnected()
{
  renderStatus->on();
}

void KMMEditPanel::rendererDisconnected()
{
  renderStatus->off();  
}

void KMMEditPanel::setInpoint()
{
  m_ruler->setSliderValue(1, m_ruler->getSliderValue(0));
}

void KMMEditPanel::setOutpoint()
{
  m_ruler->setSliderValue(2, m_ruler->getSliderValue(0));  
}
