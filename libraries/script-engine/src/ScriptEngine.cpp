//
//  ScriptEngine.cpp
//  hifi
//
//  Created by Brad Hefta-Gaub on 12/14/13.
//  Copyright (c) 2013 HighFidelity, Inc. All rights reserved.
//

#include <QtCore/QCoreApplication>
#include <QtCore/QEventLoop>
#include <QtCore/QTimer>
#include <QtCore/QThread>
#include <QtNetwork/QNetworkAccessManager>
#include <QtNetwork/QNetworkRequest>
#include <QtNetwork/QNetworkReply>

#include <AvatarData.h>
#include <NodeList.h>
#include <PacketHeaders.h>
#include <UUID.h>
#include <VoxelConstants.h>
#include <ParticlesScriptingInterface.h>

#include <Sound.h>

#include "ScriptEngine.h"

const unsigned int VISUAL_DATA_CALLBACK_USECS = (1.0 / 60.0) * 1000 * 1000;

int ScriptEngine::_scriptNumber = 1;
VoxelsScriptingInterface ScriptEngine::_voxelsScriptingInterface;
ParticlesScriptingInterface ScriptEngine::_particlesScriptingInterface;

static QScriptValue soundConstructor(QScriptContext* context, QScriptEngine* engine) {
    QUrl soundURL = QUrl(context->argument(0).toString());
    QScriptValue soundScriptValue = engine->newQObject(new Sound(soundURL), QScriptEngine::ScriptOwnership);

    return soundScriptValue;
}

ScriptEngine::ScriptEngine(const QString& scriptContents, bool wantMenuItems,
                                const char* scriptMenuName, AbstractMenuInterface* menu,
                                AbstractControllerScriptingInterface* controllerScriptingInterface) {
    _scriptContents = scriptContents;
    _isFinished = false;
    _isRunning = false;
    _isInitialized = false;

    // some clients will use these menu features
    _wantMenuItems = wantMenuItems;
    if (scriptMenuName) {
        _scriptMenuName = "Stop ";
        _scriptMenuName.append(scriptMenuName);
    } else {
        _scriptMenuName = "Stop Script ";
        _scriptNumber++;
        _scriptMenuName.append(_scriptNumber);
    }
    _menu = menu;
    _controllerScriptingInterface = controllerScriptingInterface;
}

ScriptEngine::~ScriptEngine() {
    //printf("ScriptEngine::~ScriptEngine()...\n");
}


void ScriptEngine::setupMenuItems() {
    if (_menu && _wantMenuItems) {
        _menu->addActionToQMenuAndActionHash(_menu->getActiveScriptsMenu(), _scriptMenuName, 0, this, SLOT(stop()));
    }
}

void ScriptEngine::cleanMenuItems() {
    if (_menu && _wantMenuItems) {
        _menu->removeAction(_menu->getActiveScriptsMenu(), _scriptMenuName);
    }
}

bool ScriptEngine::setScriptContents(const QString& scriptContents) {
    if (_isRunning) {
        return false;
    }
    _scriptContents = scriptContents;
    return true;
}

Q_SCRIPT_DECLARE_QMETAOBJECT(AudioInjectorOptions, QObject*)

void ScriptEngine::init() {
    if (_isInitialized) {
        return; // only initialize once
    }

    _isInitialized = true;

    _voxelsScriptingInterface.init();
    _particlesScriptingInterface.init();

    // register meta-type for glm::vec3 conversions
    registerMetaTypes(&_engine);

    QScriptValue agentValue = _engine.newQObject(this);
    _engine.globalObject().setProperty("Agent", agentValue);

    QScriptValue voxelScripterValue =  _engine.newQObject(&_voxelsScriptingInterface);
    _engine.globalObject().setProperty("Voxels", voxelScripterValue);

    QScriptValue particleScripterValue =  _engine.newQObject(&_particlesScriptingInterface);
    _engine.globalObject().setProperty("Particles", particleScripterValue);

    QScriptValue soundConstructorValue = _engine.newFunction(soundConstructor);
    QScriptValue soundMetaObject = _engine.newQMetaObject(&Sound::staticMetaObject, soundConstructorValue);
    _engine.globalObject().setProperty("Sound", soundMetaObject);

    QScriptValue injectionOptionValue = _engine.scriptValueFromQMetaObject<AudioInjectorOptions>();
    _engine.globalObject().setProperty("AudioInjectionOptions", injectionOptionValue);

    QScriptValue audioScriptingInterfaceValue = _engine.newQObject(&_audioScriptingInterface);
    _engine.globalObject().setProperty("Audio", audioScriptingInterfaceValue);

    if (_controllerScriptingInterface) {
        QScriptValue controllerScripterValue =  _engine.newQObject(_controllerScriptingInterface);
        _engine.globalObject().setProperty("Controller", controllerScripterValue);
    }

    QScriptValue treeScaleValue = _engine.newVariant(QVariant(TREE_SCALE));
    _engine.globalObject().setProperty("TREE_SCALE", treeScaleValue);

    // let the VoxelPacketSender know how frequently we plan to call it
    _voxelsScriptingInterface.getVoxelPacketSender()->setProcessCallIntervalHint(VISUAL_DATA_CALLBACK_USECS);
    _particlesScriptingInterface.getParticlePacketSender()->setProcessCallIntervalHint(VISUAL_DATA_CALLBACK_USECS);

    //qDebug() << "Script:\n" << _scriptContents << "\n";
}

void ScriptEngine::registerGlobalObject(const QString& name, QObject* object) {
    QScriptValue value = _engine.newQObject(object);
    _engine.globalObject().setProperty(name, value);
}

void ScriptEngine::evaluate() {
    if (!_isInitialized) {
        init();
    }

    QScriptValue result = _engine.evaluate(_scriptContents);
    qDebug() << "Evaluated script.\n";

    if (_engine.hasUncaughtException()) {
        int line = _engine.uncaughtExceptionLineNumber();
        qDebug() << "Uncaught exception at line" << line << ":" << result.toString() << "\n";
    }
}

void ScriptEngine::run() {
    if (!_isInitialized) {
        init();
    }
    _isRunning = true;

    QScriptValue result = _engine.evaluate(_scriptContents);
    qDebug() << "Evaluated script.\n";

    if (_engine.hasUncaughtException()) {
        int line = _engine.uncaughtExceptionLineNumber();
        qDebug() << "Uncaught exception at line" << line << ":" << result.toString() << "\n";
    }

    timeval startTime;
    gettimeofday(&startTime, NULL);

    int thisFrame = 0;

    while (!_isFinished) {
        int usecToSleep = usecTimestamp(&startTime) + (thisFrame++ * VISUAL_DATA_CALLBACK_USECS) - usecTimestampNow();
        if (usecToSleep > 0) {
            usleep(usecToSleep);
        }

        if (_isFinished) {
            break;
        }

        QCoreApplication::processEvents();

        if (_isFinished) {
            break;
        }

        bool willSendVisualDataCallBack = false;
        if (_voxelsScriptingInterface.getVoxelPacketSender()->serversExist()) {
            // allow the scripter's call back to setup visual data
            willSendVisualDataCallBack = true;

            // release the queue of edit voxel messages.
            _voxelsScriptingInterface.getVoxelPacketSender()->releaseQueuedMessages();

            // since we're in non-threaded mode, call process so that the packets are sent
            if (!_voxelsScriptingInterface.getVoxelPacketSender()->isThreaded()) {
                _voxelsScriptingInterface.getVoxelPacketSender()->process();
            }
        }

        if (_particlesScriptingInterface.getParticlePacketSender()->serversExist()) {
            // allow the scripter's call back to setup visual data
            willSendVisualDataCallBack = true;

            // release the queue of edit voxel messages.
            _particlesScriptingInterface.getParticlePacketSender()->releaseQueuedMessages();

            // since we're in non-threaded mode, call process so that the packets are sent
            if (!_particlesScriptingInterface.getParticlePacketSender()->isThreaded()) {
                _particlesScriptingInterface.getParticlePacketSender()->process();
            }
        }

        if (willSendVisualDataCallBack) {
            emit willSendVisualDataCallback();
        }

        if (_engine.hasUncaughtException()) {
            int line = _engine.uncaughtExceptionLineNumber();
            qDebug() << "Uncaught exception at line" << line << ":" << _engine.uncaughtException().toString() << "\n";
        }
    }
    cleanMenuItems();

    // If we were on a thread, then wait till it's done
    if (thread()) {
        thread()->quit();
    }

    emit finished();
    _isRunning = false;
}
void ScriptEngine::stop() {
    _isFinished = true;
}

