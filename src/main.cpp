#include <Geode/modify/PauseLayer.hpp>
#include <Geode/modify/PlayLayer.hpp>
#include <Geode/modify/EndLevelLayer.hpp>
#include <Geode/modify/GJBaseGameLayer.hpp>
#include <Geode/modify/CCKeyboardDispatcher.hpp>
#include <Geode/binding/GameManager.hpp>
#include <Geode/modify/CCScheduler.hpp>
#include <Geode/ui/GeodeUI.hpp>
#include <cocos2d.h>
#include <vector>
#include <chrono>
#include "fileSystem.hpp"

float leftOver = 0.f; // For CCScheduler
int fixedFps = 240;
bool restart = false;
bool stepFrame = false;
double prevSpeed = 1.0f;
bool safeModeEnabled = false;
bool playerHolding;

using namespace geode::prelude;



namespace safeMode
{
using opcode = std::pair<unsigned long, std::vector<uint8_t>>;

	inline const std::array<opcode, 15> codes{
		opcode{ 0x2DDC7E, { 0x0F, 0x84, 0xCA, 0x00, 0x00, 0x00 } },
		{ 0x2DDD6A, { 0x0F, 0x84, 0xEA, 0x01, 0x00, 0x00 } },
		{ 0x2DDD70, { 0x90, 0x90, 0x90, 0x90, 0x90, 0x90, 0x90 } },
		{ 0x2DDD77, { 0x90, 0x90, 0x90, 0x90, 0x90, 0x90 } },
		{ 0x2DDEE5, { 0x90 } },
		{ 0x2DDF6E, { 0x0F, 0x84, 0xC2, 0x02, 0x00, 0x00 } },

		{ 0x2E6BDE, { 0x90, 0xE9, 0xAD, 0x00, 0x00, 0x00 } },
		{ 0x2E6B32, { 0xEB, 0x0D } },
		{ 0x2E69F4, { 0x0F, 0x4C, 0xC1 } },
		{ 0x2E6993, { 0x90, 0xE9, 0x85, 0x01, 0x00, 0x00 } },

		{ 0x2EACD0, { 0x90, 0x90, 0x90, 0x90, 0x90, 0x90 } },
		{ 0x2EACD6, { 0x90, 0x90, 0x90, 0x90, 0x90, 0x90 } }, 
		{ 0x2EACF7, { 0x90 } },

		{ 0x2EA81F, { 0x6A, 0x00 } },
		{ 0x2EA83D, { 0x90 } }
	};
	inline std::array<geode::Patch*, 15> patches;

	void updateSafeMode() {
		for (auto& patch : patches) {
		if (safeModeEnabled) {
			if (!patch->isEnabled())
				patch->enable();
		}
		else {
			if (patch->isEnabled())
				patch->disable();
		}
	}
	}

}

struct playerData {
	double xPos;
	double yPos;
	bool upsideDown;
	float rotation;
	double xSpeed;
	double ySpeed;
};

struct data {
    bool player1;
    int frame;
    int button;
    bool holding;
	bool posOnly;
	playerData p1;
	playerData p2;
};

enum state {
    off,
    recording,
    playing
};

class recordSystem {
public:
    state state = off;
 	size_t currentAction = 0;
   	std::vector<data> macro;

	int currentFrame() {
		return static_cast<int>((*(double*)(((char*)PlayLayer::get()) + 0x328)) * fixedFps); // m_time * fps
	}
	void recordAction(bool holding, int button, bool player1, int frame, GJBaseGameLayer* bgl, playerData p1Data, playerData p2Data) {
		bool p1 = (GameManager::get()->getGameVariable("0010") && !bgl->m_levelSettings->m_platformerMode) ? !player1 : player1;
    	macro.push_back({p1, frame, button, holding, false, p1Data, p2Data});
	}

};

recordSystem recorder;

class RecordLayer : public geode::Popup<std::string const&> {
 	CCLabelBMFont* infoMacro = nullptr;
 	CCMenuItemToggler* recording = nullptr;
    CCMenuItemToggler* playing = nullptr;
protected:
    bool setup(std::string const& value) override {
        auto winSize = cocos2d::CCDirector::sharedDirector()->getWinSize();

		this->setTitle("xPBot");
		auto menu = CCMenu::create();
    	menu->setPosition({0, 0});
    	m_mainLayer->addChild(menu);

 		auto checkOffSprite = CCSprite::createWithSpriteFrameName("GJ_checkOff_001.png");
   		auto checkOnSprite = CCSprite::createWithSpriteFrameName("GJ_checkOn_001.png");

		CCPoint topLeftCorner = winSize/2.f-CCPOINT_CREATE(m_size.width/2.f,-m_size.height/2.f);
 
		auto label = CCLabelBMFont::create("Record", "bigFont.fnt"); 
    	label->setAnchorPoint({0, 0.5});
    	label->setScale(0.7f);
    	label->setPosition(topLeftCorner + CCPOINT_CREATE(168, -60));
    	m_mainLayer->addChild(label);

    	recording = CCMenuItemToggler::create(checkOffSprite,
		checkOnSprite,
		this,
		menu_selector(RecordLayer::toggleRecord));

    	recording->setPosition(label->getPosition() + CCPOINT_CREATE(105,0));
    	recording->setScale(0.85f);
    	recording->toggle(recorder.state == state::recording); 
    	menu->addChild(recording);

    	auto spr = CCSprite::createWithSpriteFrameName("GJ_optionsBtn_001.png");
    	spr->setScale(0.8f);
    	auto btn = CCMenuItemSpriteExtra::create(
        	spr,
        	this,
        	menu_selector(RecordLayer::openSettingsMenu)
    	);
    	btn->setPosition(winSize/2.f-CCPOINT_CREATE(m_size.width/2.f,m_size.height/2.f) + CCPOINT_CREATE(325, 20));
    	menu->addChild(btn);

		spr = CCSprite::createWithSpriteFrameName("GJ_infoIcon_001.png");
    	spr->setScale(0.65f);
    	btn = CCMenuItemSpriteExtra::create(
        	spr,
        	this,
        	menu_selector(RecordLayer::keyInfo)
    	);
    	btn->setPosition(topLeftCorner + CCPOINT_CREATE(290, -10));
    	menu->addChild(btn);

    	label = CCLabelBMFont::create("Play", "bigFont.fnt");
    	label->setScale(0.7f);
    	label->setPosition(topLeftCorner + CCPOINT_CREATE(198, -90)); 
    	label->setAnchorPoint({0, 0.5});
    	m_mainLayer->addChild(label);

     	playing = CCMenuItemToggler::create(checkOffSprite, checkOnSprite,
	 	this,
	 	menu_selector(RecordLayer::togglePlay));

    	playing->setPosition(label->getPosition() + CCPOINT_CREATE(75,0)); 
    	playing->setScale(0.85f);
    	playing->toggle(recorder.state == state::playing); 
    	menu->addChild(playing);

 		auto btnSprite = ButtonSprite::create("Save");
    	btnSprite->setScale(0.72f);

   		btn = CCMenuItemSpriteExtra::create(btnSprite,
   		this,
   		menu_selector(saveMacroPopup::openSaveMacro));

    	btn->setPosition(topLeftCorner + CCPOINT_CREATE(65, -160)); 
    	menu->addChild(btn);

		btnSprite = ButtonSprite::create("Load");
		btnSprite->setScale(0.72f);

    	btn = CCMenuItemSpriteExtra::create(btnSprite,
		this,
		menu_selector(loadMacroPopup::openLoadMenu));

    	btn->setPosition(topLeftCorner + CCPOINT_CREATE(144, -160));
    	menu->addChild(btn);

  		btnSprite = ButtonSprite::create("Clear");
		btnSprite->setScale(0.72f);

    	btn = CCMenuItemSpriteExtra::create(btnSprite,
		this,
		menu_selector(RecordLayer::clearMacro));

    	btn->setPosition(topLeftCorner + CCPOINT_CREATE(228, -160));
    	menu->addChild(btn);

		infoMacro = CCLabelBMFont::create("", "chatFont.fnt");
    	infoMacro->setAnchorPoint({0, 1});
    	infoMacro->setPosition(topLeftCorner + CCPOINT_CREATE(21, -45));
		updateInfo();
    	m_mainLayer->addChild(infoMacro);

        return true;
	}

    static RecordLayer* create() {
        auto ret = new RecordLayer();
        if (ret && ret->init(300, 200, "", "GJ_square02.png")) {
            ret->autorelease();
            return ret;
        }
        CC_SAFE_DELETE(ret);
        return nullptr;
    }

public:
 
	void openSettingsMenu(CCObject*) {
		geode::openSettingsPopup(Mod::get());
	}

	void keyInfo(CCObject*) {
		FLAlertLayer::create(
    		"Shortcuts",   
    		"<cg>Toggle Speedhack</c> = <cl>C</c>\n<cg>Advance Frame</c> = <cl>V</c>\n<cg>Disable Frame Stepper</c> = <cl>B</c>",  
    		"OK"      
		)->show();
	}

	void updateInfo() {
		int clicksCount = 0;
		if (!recorder.macro.empty()) {
			for (const data& element : recorder.macro) {
        		if (element.holding && !element.posOnly) clicksCount++;
    		}
		}
		
 		std::stringstream infoText;
    	infoText << "Current Macro:";
    	infoText << "\nSize: " << recorder.macro.size();
		infoText << "\nClicks: " << clicksCount;
		infoText << "\nDuration: " << (!recorder.macro.empty() 
		? recorder.macro.back().frame / fixedFps : 0) << "s";
    	infoMacro->setString(infoText.str().c_str());
	}

	void togglePlay(CCObject*) {
		if (recorder.state == state::recording) recording->toggle(false);
    	recorder.state = (recorder.state == state::playing) ? state::off : state::playing;

		if (recorder.state == state::playing) restart = true;
		else if (recorder.state == state::off) restart = false;
		FMODAudioEngine::sharedEngine()->setMusicTimeMS(
			(recorder.currentFrame()*1000)/240 + PlayLayer::get()->m_levelSettings->m_songOffset*1000,
			true,
			0
		);
		Mod::get()->setSettingValue("frame_stepper", false);
	}

	void toggleRecord(CCObject* sender) {
		if(!recorder.macro.empty() && recorder.state != state::recording) {
			geode::createQuickPopup(
    			"Warning",     
    			"This will <cr>clear</c> the current macro.", 
    			"Cancel", "Ok",  
    			[this, sender](auto, bool btn2) {
        			if (!btn2) this->recording->toggle(false);
			 		else {
						recorder.macro.clear();
						this->toggleRecord(sender);
					}
   				}
			);
		} else {
			if (recorder.state == state::playing) this->playing->toggle(false);
    		recorder.state = (recorder.state == state::recording) 
			? state::off : state::recording;
			if (recorder.state == state::recording) {
				restart = true;
				updateInfo();
			} else if (recorder.state == state::off) {
				restart = false;
				FMODAudioEngine::sharedEngine()->setMusicTimeMS(
					(recorder.currentFrame()*1000)/240 + PlayLayer::get()->m_levelSettings->m_songOffset*1000,
					true,
					0
				);
				Mod::get()->setSettingValue("frame_stepper", false);
			}
		}
	}

	void clearMacro(CCObject*) {
		if (recorder.macro.empty()) return;
		geode::createQuickPopup(
    	"Clear Macro",     
    	"<cr>Clear</c> the current macro?", 
    	"Cancel", "Yes",  
    	[this](auto, bool btn2) {
        	if (btn2) {
				recorder.macro.clear();
				this->updateInfo();
				if (recorder.state == state::playing) this->playing->toggle(false);
				if (recorder.state == state::recording) this->recording->toggle(false);
				recorder.state = state::off;
			}
    	});
	}

    void openMenu(CCObject*) {
		auto layer = create();
		layer->m_noElasticity = (static_cast<float>(Mod::get()->getSettingValue<double>("speedhack")) < 1
		 && recorder.state == state::recording) ? true : false;
		layer->show();
	}
};

void saveMacroPopup::openSaveMacro(CCObject*) {
	if (recorder.macro.empty()) {
		FLAlertLayer::create(
    	"Save Macro",   
    	"You can't save an <cl>empty</c> macro.",  
    	"OK"      
		)->show();
		return;
	}
	auto layer = create();
	layer->m_noElasticity = (static_cast<float>(Mod::get()->getSettingValue<double>("speedhack")) < 1
	 && recorder.state == state::recording) ? true : false;
	layer->show();
}

void saveMacroPopup::saveMacro(CCObject*) {
    if (std::string(macroNameInput->getString()).length() < 1) {
		FLAlertLayer::create(
    	"Save Macro",   
    	"Macro name can't be <cl>empty</c>.",  
    	"OK"      
		)->show();
		return;
	}
	std::string savePath = Mod::get()->getSaveDir().string()
     +"\\"+std::string(macroNameInput->getString()) + ".xd";
 	std::ofstream file(savePath);
	if (file.is_open()) {
		for (auto &action : recorder.macro) {
			file << action.frame << "|" << action.holding <<
			"|" << action.button << "|" << action.player1 <<
			"|" << action.posOnly << "|" << action.p1.xPos <<
			"|" << action.p1.yPos << "|" << action.p1.upsideDown <<
			"|" << action.p1.rotation << "|" << action.p1.xSpeed <<
			"|" << action.p1.ySpeed << "|" << action.p2.xPos <<
			"|" << action.p2.yPos << "|" << action.p2.upsideDown <<
			"|" << action.p2.rotation << "|" << action.p2.xSpeed <<
			"|" << action.p2.ySpeed  << "\n";
		}
		file.close();
		CCArray* children = CCDirector::sharedDirector()->getRunningScene()->getChildren();
		CCObject* child;
		CCARRAY_FOREACH(children, child) {
    		saveMacroPopup* saveLayer = dynamic_cast<saveMacroPopup*>(child);
    		if (saveLayer) {
        		saveLayer->keyBackClicked();
				break;
   			}
		}
        FLAlertLayer::create(
    	"Save Macro",   
    	"Macro saved <cg>succesfully</c>.",  
    	"OK"      
		)->show();
	} else {
        FLAlertLayer::create(
    	"Save Macro",   
    	"There was an <cr>error</c> saving the macro.",  
    	"OK"      
		)->show();
    }
}

void macroCell::handleLoad(CCObject* btn) {
	std::string loadPath = Mod::get()->getSaveDir().string()
    +"\\"+static_cast<CCMenuItemSpriteExtra*>(btn)->getID() + ".xd";
	recorder.macro.clear();
    std::ifstream file(loadPath);
	std::string line;
	if (!file.is_open()) {
		FLAlertLayer::create(
    	"Load Macro",   
    	"An <cr>error</c> occurred while loading this macro.",  
    	"OK"      
		)->show();
		return;
	}
	while (std::getline(file, line)) {
		std::istringstream isSS(line);
		// Do not question my methods
		playerData p1;
		playerData p2;
		int holding;
		int frame;
		int button;
		int player1;
		int posOnly;
		double p1xPos;
		double p1yPos;
		int p1upsideDown;
		double p1rotation;
		double p1xSpeed;
		double p1ySpeed;
		double p2xPos;
		double p2yPos;
		int p2upsideDown;
		double p2rotation;
		double p2xSpeed;
		double p2ySpeed;
		char s;
		int count = 0;
    	for (char ch : line) {
        	if (ch == '|') {
            	count++;
        	}
    	}
		if (count > 3) {
			if (isSS >> frame >> s >> holding >> s >> button >> 
			s >> player1 >> s >> posOnly >> s >>
			p1xPos >> s >> p1yPos >> s >> p1upsideDown
		 	>> s >> p1rotation >> s >> p1xSpeed >> s >>
		 	p1ySpeed >> s >> p2xPos >> s >> p2yPos >> s >> p2upsideDown
		 	>> s >> p2rotation >> s >> p2xSpeed >> s >>
		 	p2ySpeed && s == '|') {
				p1 = {
					(double)p1xPos,
					(double)p1yPos,
					(bool)p1upsideDown,
					(float)p1rotation,
					(double)p1xSpeed,
					(double)p1ySpeed,
				};
				p2 = {
					(double)p2xPos,
					(double)p2yPos,
					(bool)p2upsideDown,
					(float)p2rotation,
					(double)p2xSpeed,
					(double)p2ySpeed,
				};
				recorder.macro.push_back({(bool)player1, (int)frame, (int)button, (bool)holding, (bool)posOnly, p1, p2});
			}
		} else {
			if (isSS >> frame >> s >> holding >> s >> button >> 
			s >> player1 && s == '|') {
				p1.xPos = 0;
				recorder.macro.push_back({(bool)player1, (int)frame, (int)button, (bool)holding, false, p1, p2});
			}
		}
	}
	CCArray* children = CCDirector::sharedDirector()->getRunningScene()->getChildren();
	CCObject* child;
	CCARRAY_FOREACH(children, child) {
    	RecordLayer* recordLayer = dynamic_cast<RecordLayer*>(child);
    	loadMacroPopup* loadLayer = dynamic_cast<loadMacroPopup*>(child);
    	if (recordLayer) {
        	recordLayer->updateInfo();
    	} else if (loadLayer) loadLayer->keyBackClicked();
	}
	file.close();
	FLAlertLayer::create(
    "Load Macro",   
    "Macro loaded <cg>successfully</c>.",  
    "OK"      
	)->show();
}

void macroCell::loadMacro(CCObject* button) {
	if (!recorder.macro.empty()) {
		geode::createQuickPopup(
    	"Load Macro",     
    	"<cr>Overwrite</c> the current macro?", 
    	"Cancel", "Ok",  
    	[this, button](auto, bool btn2) {
        	if (btn2) this->handleLoad(button);
    	}); 
	} else handleLoad(button);
}

void clearState(bool safeMode) {
	FMOD::ChannelGroup* channel;
    FMODAudioEngine::sharedEngine()->m_system->getMasterChannelGroup(&channel);
	channel->setPitch(1);
	recorder.state = state::off;
	leftOver = 0.f;
	Mod::get()->setSettingValue("frame_stepper", false);
	if (!safeMode) {
		safeModeEnabled = false;
		safeMode::updateSafeMode();
	}
}

	// ---------------- Hooks ---------------- //

class $modify(PauseLayer) {
	void customSetup() {
		auto winSize = CCDirector::sharedDirector()->getWinSize();
        auto menu = CCMenu::create();
        menu->setPosition(winSize.width-36, winSize.height - 70.f);
        this->addChild(menu);
        auto sprite = CCSprite::createWithSpriteFrameName("GJ_stopEditorBtn_001.png");
        sprite->setScale(0.75f);

        auto btn = CCMenuItemSpriteExtra::create(sprite,
		this,
		menu_selector(RecordLayer::openMenu));

        menu->addChild(btn);
		PauseLayer::customSetup();
	} 

	void onQuit(CCObject* sender) {
		PauseLayer::onQuit(sender);
		clearState(false);
	}

	void goEdit() {
		PauseLayer::goEdit();
		clearState(false);
	}

	void onResume(CCObject* sender) {
		PauseLayer::onResume(sender);
		if (restart) PlayLayer::get()->resetLevel();
		if (recorder.state == state::off) {
			FMOD::ChannelGroup* channel;
        	FMODAudioEngine::sharedEngine()->m_system->getMasterChannelGroup(&channel);
			channel->setPitch(1);
		}
	}

	void onPracticeMode(CCObject* sender) {
		PauseLayer::onPracticeMode(sender);
		if (restart) PlayLayer::get()->resetLevel();
		if (recorder.state == state::off) {
			FMOD::ChannelGroup* channel;
        	FMODAudioEngine::sharedEngine()->m_system->getMasterChannelGroup(&channel);
			channel->setPitch(1);
		}
	}

};

class $modify(GJBaseGameLayer) {
	void handleButton(bool holding, int button, bool player1) {
		GJBaseGameLayer::handleButton(holding,button,player1);
		if (recorder.state == state::recording) {
			playerData p1;
			playerData p2;
			if (!Mod::get()->getSettingValue<bool>("vanilla") || Mod::get()->getSettingValue<bool>("frame_fix")) {
				if (!Mod::get()->getSettingValue<bool>("frame_fix")) playerHolding = holding;
				p1 = {
				this->m_player1->getPositionX(),
				this->m_player1->getPositionY(),
				this->m_player1->m_isUpsideDown,
				this->m_player1->getRotationX(),
				-50085,
				-50085
			};
			if (this->m_player2 != nullptr) {
				p2 = {
				this->m_player2->getPositionX(),
				this->m_player2->getPositionY(),
				this->m_player2->m_isUpsideDown,
				this->m_player2->getRotationX(),
				-50085,
				-50085
				};
			} else {
				p2.xPos = 0;
			}
			} else {
				p1.xPos = 0;
			}
			int frame = recorder.currentFrame(); 
			recorder.recordAction(holding, button, player1, frame, this, p1, p2);
		}
	}


	void update(float dt) {
		if (recorder.state == state::recording) {
			if (Mod::get()->getSettingValue<bool>("frame_stepper") && stepFrame == false) 
				return;
			else if (stepFrame) {
				GJBaseGameLayer::update(1.f/fixedFps);
				stepFrame = false;
				FMODAudioEngine::sharedEngine()->setMusicTimeMS(
					(recorder.currentFrame()*1000)/240 + this->m_levelSettings->m_songOffset*1000,
					true,
					0
				);
			}
		}
		GJBaseGameLayer::update(dt);
		
	}
};

void GJBaseGameLayerProcessCommands(GJBaseGameLayer* self) {
	if (recorder.state == state::recording) {
		if (((playerHolding && !Mod::get()->getSettingValue<bool>("vanilla")) ||
		Mod::get()->getSettingValue<bool>("frame_fix")) && !recorder.macro.empty()) {
			
			if (!(recorder.macro.back().frame == recorder.currentFrame() &&
			(recorder.macro.back().posOnly || recorder.macro.back().p1.xPos != 0))) {
				playerData p1 = {
					self->m_player1->getPositionX(),
					self->m_player1->getPositionY(),
					self->m_player1->m_isUpsideDown,
					self->m_player1->getRotationX(),
					-50085,
					-50085
				};
				playerData p2;
				if (self->m_player2 != nullptr) {
					p2 = {
					self->m_player2->getPositionX(),
					self->m_player2->getPositionY(),
					self->m_player2->m_isUpsideDown,
					self->m_player2->getRotationX(),
					-50085,
					-50085
					};
				} else {
					p2.xPos = 0;
				}
				recorder.macro.push_back({true,recorder.currentFrame(),1,true,true,p1,p2});
			}
		}
	}

	if (recorder.state == state::playing) {
			int frame = recorder.currentFrame();
        	while (recorder.currentAction < static_cast<int>(recorder.macro.size()) &&
			frame >= recorder.macro[recorder.currentAction].frame && !self->m_player1->m_isDead) {
            	auto& currentActionIndex = recorder.macro[recorder.currentAction];

				if (!safeModeEnabled) {
					safeModeEnabled = true;
					safeMode::updateSafeMode();
				}

				if (currentActionIndex.p1.xPos != 0) {
					if (self->m_player1->getPositionX() != currentActionIndex.p1.xPos ||
					self->m_player1->getPositionY() != currentActionIndex.p1.yPos)
						self->m_player1->setPosition(cocos2d::CCPoint(currentActionIndex.p1.xPos, currentActionIndex.p1.yPos));

					if (self->m_player1->m_isUpsideDown != currentActionIndex.p1.upsideDown && currentActionIndex.posOnly)
						self->m_player1->flipGravity(currentActionIndex.p1.upsideDown, true);

					if (self->m_player1->getRotationX() != currentActionIndex.p1.rotation && currentActionIndex.p1.rotation >= 0)
						self->m_player1->setRotationX(currentActionIndex.p1.rotation);

					if (currentActionIndex.p2.xPos != 0 && self->m_player2 != nullptr) {
						if (self->m_player2->getPositionX() != currentActionIndex.p2.xPos ||
						self->m_player2->getPositionY() != currentActionIndex.p2.yPos)
							self->m_player2->setPosition(cocos2d::CCPoint(currentActionIndex.p2.xPos, currentActionIndex.p2.yPos));

						if (self->m_player2->m_isUpsideDown != currentActionIndex.p2.upsideDown && currentActionIndex.posOnly)
							self->m_player2->flipGravity(currentActionIndex.p1.upsideDown, true);

						if (self->m_player2->getRotationX() != currentActionIndex.p2.rotation && currentActionIndex.p2.rotation >= 0)
							self->m_player2->setRotationX(currentActionIndex.p2.rotation);
					}
				}

				if (!currentActionIndex.posOnly) 
					self->handleButton(currentActionIndex.holding, currentActionIndex.button, currentActionIndex.player1);

            	recorder.currentAction++;
        	}
			if (recorder.currentAction >= recorder.macro.size()) clearState(true);
    	}
	reinterpret_cast<void(__thiscall *)(GJBaseGameLayer *)>(base::get() + 0x1BD240)(self);
}

class $modify(PlayLayer) {
	void resetLevel() {
		PlayLayer::resetLevel();
		if (recorder.state != state::off && restart != false) {
			leftOver = 0.f;
			restart = false;
		}

		safeModeEnabled = false;
		playerHolding = false;
		safeMode::updateSafeMode();

		if (recorder.state == state::playing) {
			leftOver = 0.f;
			recorder.currentAction = 0;
			FMOD::ChannelGroup* channel;
        	FMODAudioEngine::sharedEngine()->m_system->getMasterChannelGroup(&channel);
        	channel->setPitch(1);
		} else if (recorder.state == state::recording) {
        	if (this->m_isPracticeMode && !recorder.macro.empty()) {
  				int frame = recorder.currentFrame(); 
            	auto condition = [&](data& actionIndex) -> bool {
					return actionIndex.frame >= frame;
				};

            	recorder.macro.erase(remove_if(recorder.macro.begin(),
				recorder.macro.end(), condition),
				recorder.macro.end());

            	if (recorder.macro.back().holding && !recorder.macro.empty())
                	recorder.macro.push_back({
						recorder.macro.back().player1,
						frame,
						recorder.macro.back().button,
						false
					});

        	} else recorder.macro.clear();
   		}
	}

	void levelComplete() {
		PlayLayer::levelComplete();
		learState(true);c
	}
};

class $modify(EndLevelLayer) {
	void onReplay(CCObject* s) {
		EndLevelLayer::onReplay(s);
		clearState(false);
	}

	void goEdit() {
		EndLevelLayer::goEdit();
		clearState(false);
	}

	void onMenu(CCObject* s) {
		EndLevelLayer::onMenu(s);
		clearState(false);
	}
};

class $modify(CCScheduler) {
	void update(float dt) {
		if (recorder.state == state::off) return CCScheduler::update(dt);

		float speedhackValue = static_cast<float>(Mod::get()->getSettingValue<double>("speedhack"));

		if (recorder.state == state::recording) {
			FMOD::ChannelGroup* channel;
        	FMODAudioEngine::sharedEngine()->m_system->getMasterChannelGroup(&channel);
        	channel->setPitch(speedhackValue);
		} else {
			FMOD::ChannelGroup* channel;
        	FMODAudioEngine::sharedEngine()->m_system->getMasterChannelGroup(&channel);
        	channel->setPitch(1);
		}

		using namespace std::literals;
		float dt2 = (1.f / fixedFps);
		dt = (recorder.state == state::recording) ? dt * speedhackValue : dt;
    	auto startTime = std::chrono::high_resolution_clock::now();
		int mult = static_cast<int>((dt + leftOver)/dt2);  
    	for (int i = 0; i < mult; ++i) {
        	CCScheduler::update(dt2);
        	if (std::chrono::high_resolution_clock::now() - startTime > 33.333ms) {
            	mult = i + 1;
            	break;
        	}
    	}
    leftOver += (dt - dt2 * mult); 
	}
};

class $modify(CCKeyboardDispatcher) {
	bool dispatchKeyboardMSG(enumKeyCodes key, bool hold, bool p) {
		if (key == cocos2d::enumKeyCodes::KEY_C && hold && !p && recorder.state == state::recording) {
			if (!Mod::get()->getSettingValue<bool>("disable_speedhack")) {
				if (prevSpeed != 1 && Mod::get()->getSettingValue<double>("speedhack") == 1)
					Mod::get()->setSettingValue("speedhack", prevSpeed);
				else {
					prevSpeed = Mod::get()->getSettingValue<double>("speedhack");
					Mod::get()->setSettingValue("speedhack", 1.0);
				}
			}
		}

		if (key == cocos2d::enumKeyCodes::KEY_V && hold && !p && recorder.state == state::recording) {
			if (!Mod::get()->getSettingValue<bool>("disable_frame_stepper")) {
				if (Mod::get()->getSettingValue<bool>("frame_stepper")) stepFrame = true;
				else Mod::get()->setSettingValue("frame_stepper", true);
			}
		}

		if (key == cocos2d::enumKeyCodes::KEY_B && hold && !p && recorder.state == state::recording) {
			if (Mod::get()->getSettingValue<bool>("frame_stepper")) {
				FMODAudioEngine::sharedEngine()->setMusicTimeMS(
					(recorder.currentFrame()*1000)/240 + PlayLayer::get()->m_levelSettings->m_songOffset*1000,
					true,
					0
				);
				Mod::get()->setSettingValue("frame_stepper", false);
			}
		}
		return CCKeyboardDispatcher::dispatchKeyboardMSG(key,hold,p);
	}
};

$execute {
	Mod::get()->hook(reinterpret_cast<void *>(base::get() + 0x1BD240), &GJBaseGameLayerProcessCommands, "GJBaseGameLayer::processCommands", tulip::hook::TulipConvention::Thiscall);
	for (std::size_t i = 0; i < 15; i++) {
		safeMode::patches[i] = Mod::get()->patch(reinterpret_cast<void*>(base::get() + std::get<0>(safeMode::codes[i])),
		std::get<1>(safeMode::codes[i])).unwrap();
		safeMode::patches[i]->disable();
	}
}
