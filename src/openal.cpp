#include "cbase.h"
#include "openal.h"
#include "c_basehlplayer.h" // For listener syncronization

// This will tell OpenAL to start running the demo automatically.
//#define OPENAL_AUTOSTART_DEMO

COpenALUpdateThread    g_OpenALUpdateThread;
COpenALGameSystem      g_OpenALGameSystem;

/**********
 * Methods for the OpenAL manager itself.
 **********/
COpenALGameSystem::COpenALGameSystem()
{
}

COpenALGameSystem::~COpenALGameSystem()
{
}

bool COpenALGameSystem::Add(IOpenALSample *sample)
{
	AUTO_LOCK_FM(m_vSamples);
	m_vSamples.InsertBefore(0, sample);
	return true;
}

bool COpenALGameSystem::Init()
{
	float gain = 0.0f;

	m_alDevice = alcOpenDevice((ALCchar*) "Generic Hardware");

	if (m_alDevice == NULL)
	{
		Warning("OpenAL: Device couldn't be properly opened. Initialization failed.\n");
		return false;
	}

	m_alContext = alcCreateContext(m_alDevice, NULL);

	if (alGetError() != AL_NO_ERROR)
	{
		Warning("OpenAL: Couldn't create an OpenAL context. Initialization failed.\n");
		return false;
	}

	alcMakeContextCurrent(m_alContext);
	if (alGetError() != AL_NO_ERROR)
	{
		Warning("OpenAL: Couldn't make the OpenAL context current.\n");
		return false;
	}

	// Initialize this to zero in order to prevent loud audio before we get the volume ConVar(s).
	alListenerfv(AL_GAIN, &gain);
	if (alGetError() != AL_NO_ERROR)
	{
		Warning("OpenAL: Couldn't change gain? This could get loud... Continuing without regard.\n");
	}

	// Set up the speed of sound. If this doesn't work right, you have old drivers.
	alSpeedOfSound(valveSpeedOfSound);
	if (alGetError() != AL_NO_ERROR)
	{
		Warning("OpenAL: You need to update your audio drivers or OpenAL for sound to work properly.\n");
		return false;
	}

	m_bInitialized = true;

	Update(-1);

	if (!g_OpenALUpdateThread.IsAlive())
		g_OpenALUpdateThread.Start();

#ifdef OPENAL_AUTOSTART_DEMO
	engine->ClientCmd("openal_ogg_demo_play\n");
#endif

	return true;
}

void COpenALGameSystem::Shutdown()
{
	if (g_OpenALUpdateThread.IsAlive())
		g_OpenALUpdateThread.CallWorker(COpenALUpdateThread::EXIT);

	AUTO_LOCK_FM(m_vSamples);
	for (int i=0; i < m_vSamples.Count(); i++)
	{
		delete m_vSamples[i];
	}

	m_vSamples.RemoveAll();

	if (m_alDevice != NULL)
	{
		if (m_alContext != NULL)
		{
			alcMakeContextCurrent(NULL);
			alcDestroyContext(m_alContext);
			m_alContext = NULL;
		}

		alcCloseDevice(m_alDevice);
		m_alDevice = NULL;
	}
}


/***
 * This is the standard IGameSystem inherited Update section which we will be using
 * in order to syncronize OpenAL with other various game systems. Basically, anything
 * put here doesn't have to worry about thread locking - but this method shouldn't ever
 * touch the sample vector. If it absolutely has to, don't forget to properly lock it the
 * same way that UpdateSamples() does.
 ***/
void COpenALGameSystem::Update(float frametime)
{
	UpdateListener(frametime);
}

/***
 * Updates listener information. This is inline because it's only separated for
 * organization purposes. It really doesn't need to be separated in the stack.
 ***/
inline void COpenALGameSystem::UpdateListener(const float frametime)
{
	float position[3], orientation[6], gain=0.0f;
	Vector earPosition, fwd, right, up;
	CBasePlayer *localPlayer;
	ConVar *pVolume;

	pVolume = cvar->FindVar("volume");
	if (pVolume) gain = pVolume->GetFloat();

	localPlayer = CBasePlayer::GetLocalPlayer();

	if (localPlayer)
	{
		earPosition = localPlayer->EarPosition();
		AngleVectors(localPlayer->EyeAngles(), &fwd, &right, &up);

		position[0] = earPosition.x;
		position[1] = earPosition.y;
		position[2] = earPosition.z;

		orientation[0] = fwd.x;
		orientation[1] = fwd.y;
		orientation[2] = fwd.z;
		orientation[3] = up.x;
		orientation[4] = up.y;
		orientation[5] = up.z;
	}
	else
	{
		position[0] = 0.0f;
		position[1] = 0.0f;
		position[2] = 0.0f;

		orientation[0] = 0.0f;
		orientation[1] = 0.0f;
		orientation[2] = -1.0f; // Should this be positive or negative?
		orientation[3] = 0.0f;
		orientation[4] = 0.0f;
		orientation[5] = 0.0f;
	}

	alListenerfv(AL_POSITION,    position);
	if (alGetError() != AL_NO_ERROR)
		Warning("OpenAL: Couldn't update the listener's position.\n");

	alListenerfv(AL_ORIENTATION, orientation);
	if (alGetError() != AL_NO_ERROR)
		Warning("OpenAL: Couldn't update the listener's orientation.\n");

	alListenerfv(AL_GAIN,        &gain);
	if (alGetError() != AL_NO_ERROR)
		Warning("OpenAL: Couldn't properly set the listener's gain.\n");
}

/***
 * This is where streams are actually buffered, played, etc. This is called repeatedly
 * by the thread process, and therefore need not be called from Update().
 ***/
void COpenALGameSystem::UpdateSamples(const float updateTime)
{
	/**
	 * For safe thread execution, we need to lock the Vector before accessing it.
	 * This macro declares a class with inline constructors/destructors allowing
	 * automatic closing of the mutex once the vector leaves the stack.
	 **/
	AUTO_LOCK_FM(m_vSamples);

	// Update our samples.
	for (int i=0; i < m_vSamples.Count(); ++i)
	{
		if (m_vSamples[i]) // Verify that this pointer
		{
			if (m_vSamples[i]->IsReady())
				m_vSamples[i]->Update(updateTime);

			if (m_vSamples[i]->IsFinished())
				m_vSamples.Remove(i);
		}
		else
		{
			m_vSamples.Remove(i);
		}
	}
}

// Gets the full path of a specified sound file relative to the /sound folder
void COpenALGameSystem::GetSoundPath(const char* relativePath, char* buffer, size_t bufferSize)
{
  Q_snprintf(buffer, bufferSize, "%s/sound/%s", engine->GetGameDirectory(), relativePath);

  for (; *buffer; ++buffer) {
    if (*buffer == '\\') *buffer = '/';
  }
}

/*********
 * Methods for the update thread.
 *********/
COpenALUpdateThread::COpenALUpdateThread()
{
	SetName("OpenALUpdateThread");
}

COpenALUpdateThread::~COpenALUpdateThread()
{
}

bool COpenALUpdateThread::Init()
{
	return true;
}

void COpenALUpdateThread::OnExit()
{
}

// This is our main loop for the OpenAL update thread.
int COpenALUpdateThread::Run()
{
	unsigned nCall;

	while (IsAlive())
	{
		// Make sure this thread isn't ready to exit yet.
		if (PeekCall(&nCall))
		{
			// If this thread has been asked to safely stop processing...
			if (nCall == EXIT)
			{
				Reply(1);
				break;
			}
		}

		// Otherwise, let's keep those speakers pumpin'
		g_OpenALGameSystem.UpdateSamples(gpGlobals->curtime);
	}

	return 0;
}