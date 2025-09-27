//========= Copyright Valve Corporation, All rights reserved. ============//
//
// Purpose:		Load item upgrade data from KeyValues
//
// $NoKeywords: $
//=============================================================================

#ifndef TF_UPGRADES_SHARED_H
#define TF_UPGRADES_SHARED_H

#ifdef CLIENT_DLL
#define CTFPlayer C_TFPlayer
#endif

class CTFPlayer;

class CMannVsMachineUpgrades
{
public:
	char szAttrib[ MAX_ATTRIBUTE_DESCRIPTION_LENGTH ];
	char szIcon[ MAX_PATH ];
	char szGroup[ MAX_PATH ];
	float flIncrement;
	float flCap;
	float flMult;
	float flCostMult;
	int nCost;
	int nUIGroup;
	int nQuality;
	int nTier;		// If set, upgrades in the same tier - for the same player/item - will be mutually exclusive
};


class CMannVsMachineUpgradeGroup
{
public:
	char szName[ MAX_ATTRIBUTE_DESCRIPTION_LENGTH ];
	CUtlMap< const char*, int > m_ConditionMap;
};

class CMannVsMachineUpgradeManager : public CAutoGameSystem
{
public:
	CMannVsMachineUpgradeManager();

	virtual void LevelInitPostEntity();
	virtual void LevelShutdownPostEntity();

	void ParseUpgradeBlockForUIGroup( KeyValues *pKV, int iDefaultUIGroup );

	void ParseUpgradeGroupBlock( KeyValues *pKV );

	int GetAttributeIndexByName( const char* pszAttributeName );

	void LoadUpgradesFile( void );
	void LoadUpgradesFileFromPath( const char *pszPath );

	bool GroupResult( const char* pszGroupName, CTFPlayer *pPlayer, int iWeaponSlot );

	bool IsAttribValid( CMannVsMachineUpgrades *pUpgrade, CTFPlayer *pPlayer, int iWeaponSlot );

public:
	CUtlVector< CMannVsMachineUpgrades > m_Upgrades;
	CUtlVector< CMannVsMachineUpgradeGroup > m_UpgradeGroups;

private:
	CUtlMap< const char*, int > m_AttribMap;

};

extern CMannVsMachineUpgradeManager g_MannVsMachineUpgrades;
int GetUpgradeStepData( CTFPlayer *pPlayer, int nWeaponSlot, int nUpgradeIndex, int &nCurrentStep, bool &bOverCap );

#endif // TF_UPGRADES_H
