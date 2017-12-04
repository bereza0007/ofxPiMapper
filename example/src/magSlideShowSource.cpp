//
// magSlideShowSource.cpp
// Copyright (c) 2017 Cristobal Mendoza
// http://cuppetellimendoza.com
//


#include "magSlideShowSource.h"
#include "magSlideTransition.h"
#include "SettingsLoader.h"
#include "magSlideTransitionFactory.h"

magSlideShowSource::magSlideShowSource()
{
	name = "Slide Show Source";
	currentSlideIndex = 0;
	isPlaying = false;
}

bool magSlideShowSource::initialize(magSlideShowSource::Settings settings)
{
	this->settings = settings;
	bool success = true;

	if (settings.width <= 0 || settings.height <= 0)
	{
		ofLogError("magSlideShowSource::initialize") << "Invalid value for width or height. Width and height "
				"must be assigned in your Settings struct!";
		return false;
	}

	// Allocate the FBO:
	allocate(settings.width, settings.height);

	// If there is a path in slidesFolderPath, attempt
	// to load the folder and any files in it:
	if (!settings.slidesFolderPath.empty())
	{
//        ofDirectory dir = ofDirectory(settings.slidesFolderPath);
		success = createFromFolderContents(settings.slidesFolderPath);

		if (!success)
		{
			ofLogError("magSlideShowSource::initialize") << "Failed to create slide show from folder "
														 << settings.slidesFolderPath;
			return success;
		}

	}
	else if (!settings.slideshowFilePath.empty())
	{
		// try to load slide show from xml
		success = false;
	}
	return success;
}

void magSlideShowSource::setup()
{
	ofx::piMapper::FboSource::setup();
}

void magSlideShowSource::update()
{
	if (!isPlaying) return;

	auto nowTime = ofGetElapsedTimeMillis();
	deltaTime = nowTime-lastTime;
	runningTime += deltaTime;
	lastTime = nowTime;

//    ofLogVerbose() << "Delta: " << deltaTime << " running: " << runningTime;

	for (auto &slide : activeSlides)
	{
		if (slide->activeTransition)
		{
			slide->activeTransition->update(deltaTime);
		}
		slide->update(deltaTime);
	}

	// Erase any complete slides:
	auto iter = activeSlides.begin();
	for (; iter < activeSlides.end(); iter++)
	{
		if ((*iter)->isSlideComplete())
		{
//            ofLogVerbose() << "Removing from active slides id: " << (*iter)->getId();
			activeSlides.erase(iter);
			--iter;
		}
	}

	if (activeSlides.size() == 0 && isPlaying)
	{
		ofEventArgs args;
		isPlaying = false;
		ofNotifyEvent(slideshowCompleteEvent, args, this);
	}
}

void magSlideShowSource::draw()
{
	ofBackground(0, 0);
	ofPushMatrix();
	ofPushStyle();
	ofTranslate(getWidth()/2.0f, getHeight()/2.0f);
	ofEnableAlphaBlending();
	ofSetRectMode(OF_RECTMODE_CENTER);
	ofFill();
	ofSetColor(255, 255);
	for (auto &slide : activeSlides)
	{
		if (slide->activeTransition)
		{
			slide->activeTransition->draw();
		}
		slide->draw();
	}
	ofPopStyle();
	ofPopMatrix();
	ofDisableAlphaBlending();
}

bool magSlideShowSource::createFromFolderContents(std::string path)
{
	ofDirectory dir = ofDirectory(path);
	slides.clear();

	if (!dir.isDirectory())
	{
		ofLogError("magSlideShowSource::createFromFolderContents") << "Folder path " << dir.getAbsolutePath()
																   << " is not a directory";
		return false;
	}

	auto sortedDir = dir.getSorted();
	auto files = sortedDir.getFiles();

	if (files.size() < 1)
	{
		ofLogError("magSlideShowSource::createFromFolderContents") << "Folder " << dir.getAbsolutePath() << " is empty";
		return false;
	}

	ofImage tempImage;
	for (auto &file : files)
	{
		if (tempImage.load(file))
		{
			// make a new image slide
			auto slide = std::make_shared<magImageSlide>();
			slide->setup(tempImage);
			slide->setDuration(static_cast<u_int64_t>(settings.slideDuration*1000));
			slide->setTransitionDuration(static_cast<u_int64_t>(settings.transitionDuration*1000));
//            if (settings.transitionName == "")
			addSlide(slide);
		}
		else
		{
			auto ext = ofToLower(file.getExtension());

			static std::vector<std::string> movieExtensions = {
					"mov", "qt",                                // Mac
					"mp4", "m4p", "m4v",                        // MPEG
					"mpg", "mp2", "mpeg", "mpe", "mpv", "m2v",  // MPEG
					"3gp",                                      // Phones
					"avi", "wmv", "asf",                        // Windows
					"webm", "mkv", "flv", "vob",                // Other containers
					"ogv", "ogg",
					"drc", "mxf"
			};

			// Check if the extension matches known movie formats:
			if (ofContains(movieExtensions, ext))
			{
				// Make a new video slide
				auto slide = std::make_shared<magVideoSlide>();
				if (slide->setup(file))
				{
					slide->setDuration(settings.slideDuration*1000.0);
					slide->setTransitionDuration(settings.transitionDuration*1000.0);
					addSlide(slide);
				}
				else
				{
					ofLogError("magSlideShowSource") << "Failed loading video: " << file.getAbsolutePath();
				}
			}

		}
	}

	if (slides.size() > 0)
	{
		return true;
	}
	else
	{
		return false;
	}
}

bool magSlideShowSource::loadFromXml()
{
	auto *loader = ofx::piMapper::SettingsLoader::instance();
	auto xml = ofxXmlSettings();
	Settings settings;

	if (!xml.load(loader->getLastLoadedFilename()))
	{
		ofLogError("magSlideShowSource") << "Could not load settings file " << loader->getLastLoadedFilename();
		return false;
	}

	xml.pushTag("surfaces");
	if (!xml.pushTag("magSlideShow"))
	{
		ofLogError("magSlideShowSource") << "Slide show settings not found in " << loader->getLastLoadedFilename();
		return false;
	}

	settings.width = xml.getValue("Width", settings.width);
	settings.height = xml.getValue("Height", settings.height);

	// Default slide duration:
	settings.slideDuration = xml.getValue("SlideDuration", settings.slideDuration);

	// Default loop:
	if (xml.pushTag("Loop"))
	{
		auto type = xml.getValue("Type", "");
		if (type == "NONE")
		{
			settings.loopType = LoopType::NONE;
		}
		else if (type == "NORMAL")
		{
			settings.loopType = LoopType::NORMAL;
		}
		else if (type == "PING-PONG")
		{
			settings.loopType = LoopType::PING_PONG;
		}

		settings.numLoops = xml.getValue("Count", settings.numLoops);
		xml.popTag();
	}

	// Default resize options:
	auto ropts = xml.getValue("ResizeOption", "");
	if (ropts == "NoResize")
	{
		settings.resizeOption = magSlide::NoResize;
	}
	else if (ropts == "Native")
	{
		settings.resizeOption = magSlide::Native;
	}
	else if (ropts == "Fit")
	{
		settings.resizeOption = magSlide::Fit;
	}
	else if (ropts == "FitProportionally")
	{
		settings.resizeOption = magSlide::FitProportionally;
	}
	else if (ropts == "FillProportionally")
	{
		settings.resizeOption = magSlide::FillProportionally;
	}

	settings.transitionName = "FadeIn";
	settings.transitionDuration = 1.0;
	initialize(settings);

	return true;

}

void magSlideShowSource::addSlide(std::shared_ptr<magSlide> slide)
{
//	ofLogVerbose("addSlide") << slide->getId();
	slides.push_back(slide);
	auto rOption = slide->getResizeOption();

	// If the slide does not have a resize option assign
	// the slide show's option
	if (rOption == magSlide::ResizeOptions::NoResize)
	{
		rOption = settings.resizeOption;
	}

	// Resize the slide according to the resize option:
	switch (rOption)
	{
		float sw, sh, ratio;

		case magSlide::ResizeOptions::FitProportionally:
			sw = slide->getWidth();
			sh = slide->getHeight();

			if (sw > sh)
			{
				ratio = (float) getWidth()/sw;
			}
			else
			{
				ratio = (float) getHeight()/sh;
			}

			slide->setSize(sw*ratio, sh*ratio);
			break;

		case magSlide::ResizeOptions::FillProportionally:
			sw = slide->getWidth();
			sh = slide->getHeight();

			if (sw > sh)
			{
				ratio = (float) getHeight()/sh;
			}
			else
			{
				ratio = (float) getWidth()/sw;
			}

			slide->setSize(sw*ratio, sh*ratio);
			break;

		case magSlide::Fit:
			slide->setSize(getWidth(), getHeight());
			break;
	}

	// Add transitions:

	if (!settings.transitionName.empty())
	{
		static ofParameterGroup bogusParamGroup; // This is temporary so that things compile

		auto tf = magSlideTransitionFactory::instance();
		slide->buildIn = tf->createTransition(settings.transitionName,
															  slide,
															  bogusParamGroup,
															  slide->buildInDuration);
		slide->buildOut = tf->createTransition(settings.transitionName,
															   slide,
															   bogusParamGroup,
															   slide->buildOutDuration);
	}
	////     void method(const void * sender, ArgumentsType &args)
	ofAddListener(slide->slideStateChangedEvent, this, &magSlideShowSource::slideStateChanged);
	ofAddListener(slide->slideCompleteEvent, this, &magSlideShowSource::slideComplete);

}

void magSlideShowSource::play()
{
	if (!isPlaying)
	{
		runningTime = 0;
		lastTime = ofGetElapsedTimeMillis();
		isPlaying = true;
		auto currentSlide = slides[currentSlideIndex];
		enqueueSlide(currentSlide, ofGetElapsedTimeMillis());
	}
}

void magSlideShowSource::pause()
{
	isPlaying = false;
}

void magSlideShowSource::playNextSlide()
{
	//TODO
	// I should check here to see if there are less than two slides.
	// If so, we should probably return

	currentSlideIndex += direction;
	ofEventArgs args;

	// This makes sure that we are doing a signed integer comparison,
	// otherwise things get weird
	int num = slides.size();
	switch (settings.loopType)
	{
		case LoopType::NONE:
			if (currentSlideIndex >= slides.size() || currentSlideIndex < 0)
			{
				// If we are not looping and we are out of bounds, return
				// without enqueueing a slide. This will cause the slide show
				// to end once the last slide builds out.
				return;
			}
			break;
		case LoopType::NORMAL:
			if (currentSlideIndex >= num)
			{
				loopCount++;
				if (loopCount == settings.numLoops)
				{
					// Return without enqueueing a new slide if we have
					// reached the max number of loops.
					return;
				}
				currentSlideIndex = 0;
				ofNotifyEvent(slideshowWillLoopEvent, args, this);
			}
			else if (currentSlideIndex < 0)
			{
				loopCount++;
				if (loopCount == settings.numLoops)
				{
					// Return without enqueueing a new slide if we have
					// reached the max number of loops.
					return;
				}
				currentSlideIndex = slides.size()-1;
				ofNotifyEvent(slideshowWillLoopEvent, args, this);
			}
			break;
		case LoopType::PING_PONG:

			int num = slides.size();
			if (currentSlideIndex >= num)
			{
				loopCount++;
				if (loopCount == settings.numLoops)
				{
					// Return without enqueueing a new slide if we have
					// reached the max number of loops.
					return;
				}

				direction = -1;
				currentSlideIndex = slides.size()-2;
				ofNotifyEvent(slideshowWillLoopEvent, args, this);
			}
			else if (currentSlideIndex < 0)
			{
				loopCount++;
				if (loopCount == settings.numLoops)
				{
					// Return without enqueueing a new slide if we have
					// reached the max number of loops.
					return;
				}

				direction = 1;
				currentSlideIndex = 1;
				ofNotifyEvent(slideshowWillLoopEvent, args, this);
			}
			break;
	}

	enqueueSlide(slides[currentSlideIndex], ofGetElapsedTimeMillis());
}

void magSlideShowSource::playPrevSlide()
{
	currentSlideIndex -= (direction*2);
	playNextSlide();
}

void magSlideShowSource::playSlide(int slideIndex)
{
	currentSlideIndex = slideIndex-direction;
	playNextSlide();
}

void magSlideShowSource::enqueueSlide(std::shared_ptr<magSlide> slide, u_int64_t startTime)
{
//	ofLogVerbose() << "Enqueuing slide " << currentSlideIndex << " slide id: " << slide->getId();
	slide->start(startTime);
	activeSlides.push_back(slide);
}

void magSlideShowSource::slideStateChanged(const void *sender, ofEventArgs &args)
{
	magSlide *slide = (magSlide *) sender;

//	ofLogVerbose("slideStateChanged") << "Slide id: " << slide->getId() << " Slide state: "
//									  << slide->getSlideStateName();
	if (slide->getSlideState() == magSlide::SlideState::BuildOut)
	{
		playNextSlide();
	}

}

void magSlideShowSource::slideComplete(const void *sender, ofEventArgs &args)
{
	magSlide *slide = (magSlide *) sender;
//	ofLogVerbose() << "Slide Complete. id: " << slide->getId();
	slide->isComplete = true;
}


