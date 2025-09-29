//========= Copyright Valve Corporation, All rights reserved. ============//
//
// Purpose:		Load item upgrade data from KeyValues
//
// $NoKeywords: $
//=============================================================================

#include "cbase.h"

#include "tf_shareddefs.h"
#include "tf_upgrades_shared.h"
#include "filesystem.h"
#include "econ_item_system.h"
#include "tf_gamerules.h"
#include "tf_item_powerup_bottle.h"
#include "tf_weaponbase_gun.h"

#include "tf_weaponbase_melee.h"
#include "tf_wearable_weapons.h"
#include "tf_weapon_buff_item.h"
#include "tf_weapon_flamethrower.h"
#include "tf_weapon_medigun.h"

CMannVsMachineUpgradeManager g_MannVsMachineUpgrades;

CMannVsMachineUpgradeManager::CMannVsMachineUpgradeManager()
{
	SetDefLessFunc( m_AttribMap );
}

//-----------------------------------------------------------------------------
// Purpose: 
//-----------------------------------------------------------------------------
void CMannVsMachineUpgradeManager::LevelInitPostEntity()
{
	LoadUpgradesFile();
}

//-----------------------------------------------------------------------------
// Purpose: 
//-----------------------------------------------------------------------------
void CMannVsMachineUpgradeManager::LevelShutdownPostEntity()
{
	m_Upgrades.RemoveAll();
	m_UpgradeGroups.RemoveAll();
}

//-----------------------------------------------------------------------------
// Purpose: 
//-----------------------------------------------------------------------------
void CMannVsMachineUpgradeManager::ParseUpgradeBlockForUIGroup( KeyValues *pKV, int iDefaultUIGroup )
{
	if ( !pKV )
		return;

	for ( KeyValues *pData = pKV->GetFirstSubKey(); pData != NULL; pData = pData->GetNextKey() )
	{
		// Check that the expected data is there
		KeyValues *pkvAttribute = pData->FindKey( "attribute" );
		KeyValues *pkvIcon = pData->FindKey( "icon" );
		KeyValues *pkvIncrement = pData->FindKey( "increment" );
		KeyValues *pkvCap = pData->FindKey( "cap" );
		KeyValues *pkvCost = pData->FindKey( "cost" );
		if ( !pkvAttribute || !pkvIcon || !pkvIncrement || !pkvCap || !pkvCost )
		{
			Warning( "Upgrades: One or more upgrades missing attribute, icon, increment, cap, or cost value.\n" );
			return;
		}

		int index = m_Upgrades.AddToTail();

		const char *pszAttrib = pData->GetString( "attribute" );
		V_strncpy( m_Upgrades[ index ].szAttrib, pszAttrib, sizeof( m_Upgrades[ index ].szAttrib ) );
		const CEconItemSchema *pSchema = ItemSystem()->GetItemSchema();
		if ( pSchema )
		{
			// If we can't find a matching attribute, nuke this entry completely
			const CEconItemAttributeDefinition *pAttr = pSchema->GetAttributeDefinitionByName( m_Upgrades[ index ].szAttrib );
			if ( !pAttr )
			{
				Warning( "Upgrades: Invalid attribute reference! -- %s.\n", m_Upgrades[ index ].szAttrib );
				m_Upgrades.Remove( index );
				continue;
			}
			Assert( pAttr->GetAttributeType() );
			if ( !pAttr->GetAttributeType()->BSupportsGameplayModificationAndNetworking() )
			{
				Warning( "Upgrades: Invalid attribute '%s' is of a type that doesn't support networking!\n", m_Upgrades[ index ].szAttrib );
				m_Upgrades.Remove( index );
				continue;
			}
			if ( !pAttr->IsStoredAsFloat() || pAttr->IsStoredAsInteger() )
			{
				Warning( "Upgrades: Attribute reference '%s' is not stored as a float!\n", m_Upgrades[ index ].szAttrib );
				m_Upgrades.Remove( index );
				continue;
			}
		}

		V_strncpy( m_Upgrades[index].szIcon, pData->GetString( "icon" ), sizeof( m_Upgrades[ index ].szIcon ) );
		m_Upgrades[ index ].flIncrement = pData->GetFloat( "increment" );
		m_Upgrades[ index ].flCap = pData->GetFloat( "cap" );
		m_Upgrades[ index ].nCost = pData->GetInt( "cost" );
		m_Upgrades[ index ].nUIGroup = pData->GetInt( "ui_group", iDefaultUIGroup );
		m_Upgrades[ index ].nQuality = pData->GetInt( "quality", MVM_UPGRADE_QUALITY_NORMAL );
		m_Upgrades[ index ].nTier = pData->GetInt( "tier", 0 );
		V_strncpy( m_Upgrades[index].szGroup, pData->GetString( "group", "default" ), sizeof( m_Upgrades[index].szGroup ) );
		m_Upgrades[ index ].flMult = pData->GetFloat( "mult", 1.f );
		m_Upgrades[ index ].flCostMult = pData->GetFloat( "cost_mult", 1.f );
	}
}

//-----------------------------------------------------------------------------
// Purpose: 
//-----------------------------------------------------------------------------
void CMannVsMachineUpgradeManager::ParseUpgradeGroupBlock( KeyValues *pKV )
{
	if ( !pKV )
		return;

	for ( KeyValues *pData = pKV->GetFirstSubKey(); pData != NULL; pData = pData->GetNextKey() )
	{
		int index = m_UpgradeGroups.AddToTail();

		const char *pszName = pData->GetName();
		V_strncpy( m_UpgradeGroups[ index ].szName, pszName, sizeof( m_UpgradeGroups[ index ].szName ) );

		SetDefLessFunc( m_UpgradeGroups[ index ].m_ConditionMap );

		for ( KeyValues* pSubData = pData->GetFirstSubKey(); pSubData != NULL; pSubData = pSubData->GetNextKey() )
		{
			const char *pszCondition = pSubData->GetName();
			int iValue = pSubData->GetInt();

			m_UpgradeGroups[ index ].m_ConditionMap.Insert( pszCondition, iValue );
		}
	}
}

//-----------------------------------------------------------------------------
// Purpose: 
//-----------------------------------------------------------------------------
bool CMannVsMachineUpgradeManager::IsAttribValid( CMannVsMachineUpgrades *pUpgrade, CTFPlayer *pPlayer, int iWeaponSlot )
{
	// Search for the upgrade group
	const char* szGroupName = pUpgrade->szGroup;

	// Default case
	if ( FStrEq( szGroupName, "default" ) )
	{
		return true;
	}

	for ( int i = 0, nCount = m_UpgradeGroups.Count(); i < nCount; ++i )
	{
		if ( FStrEq( szGroupName, m_UpgradeGroups[i].szName ) )
		{
			// Now search that group within the upgrade group
			bool bExclude = false; // Keeps track of values that either say its invalid if cond is true, or is only valid if everything else meets this condition (0/2)
			bool bAllowed = false; // Does the item meet any criteria (1)

			int idx = 0;
			FOR_EACH_MAP_FAST( m_UpgradeGroups[i].m_ConditionMap, idx )
			{
				int iType = m_UpgradeGroups[i].m_ConditionMap[idx];
				const char* szSubGroup = m_UpgradeGroups[i].m_ConditionMap.Key(idx);

				if ( iType == 0 ) // If condition is met, attrib is invalid
				{
					bExclude = bExclude || GroupResult( szSubGroup, pPlayer, iWeaponSlot );
				}
				else if ( iType == 1 ) // If condition is met, attrib is valid
				{
					bAllowed = bAllowed || GroupResult( szSubGroup, pPlayer, iWeaponSlot );
				}
				else if ( iType == 2 ) // If condition is not met, attrib is invalid
				{
					bExclude = bExclude || ( !GroupResult( szSubGroup, pPlayer, iWeaponSlot ) );
				}
			}

			return bAllowed && !bExclude;
		}
	}

	return false;
}

//-----------------------------------------------------------------------------
// Purpose: 
//-----------------------------------------------------------------------------
bool CMannVsMachineUpgradeManager::GroupResult( const char* pszGroupName, CTFPlayer *pPlayer, int iWeaponSlot )
{
	if ( !pPlayer )
		return false;

	// These don't require a weapon to test
	if ( FStrEq( pszGroupName, "default" ) )
	{
		return true;
	}
	else if ( FStrEq( pszGroupName, "scout" ) )
	{
		return pPlayer->IsPlayerClass( TF_CLASS_SCOUT );
	}
	else if ( FStrEq( pszGroupName, "soldier" ) )
	{
		return pPlayer->IsPlayerClass( TF_CLASS_SOLDIER );
	}
	else if ( FStrEq( pszGroupName, "pyro" ) )
	{
		return pPlayer->IsPlayerClass( TF_CLASS_PYRO );
	}
	else if ( FStrEq( pszGroupName, "demoman" ) )
	{
		return pPlayer->IsPlayerClass( TF_CLASS_DEMOMAN );
	}
	else if ( FStrEq( pszGroupName, "heavy" ) )
	{
		return pPlayer->IsPlayerClass( TF_CLASS_HEAVYWEAPONS );
	}
	else if ( FStrEq( pszGroupName, "engineer" ) )
	{
		return pPlayer->IsPlayerClass( TF_CLASS_ENGINEER );
	}
	else if ( FStrEq( pszGroupName, "medic" ) )
	{
		return pPlayer->IsPlayerClass( TF_CLASS_MEDIC );
	}
	else if ( FStrEq( pszGroupName, "sniper" ) )
	{
		return pPlayer->IsPlayerClass( TF_CLASS_SNIPER );
	}
	else if ( FStrEq( pszGroupName, "spy" ) )
	{
		return pPlayer->IsPlayerClass( TF_CLASS_SPY );
	}

	// Get the item entity. We use the entity, not the item in the loadout, because we want
	// the dynamic attributes that have already been purchases and attached.
	CEconEntity *pEntity;
	CEconItemView *pCurItemData = CTFPlayerSharedUtils::GetEconItemViewByLoadoutSlot( pPlayer, iWeaponSlot, &pEntity );
	if ( !pCurItemData || !pEntity )
		return false;

	CTFWeaponBase *pWeapon = dynamic_cast< CTFWeaponBase* > ( pEntity );
	CTFWeaponBaseGun *pWeaponGun = dynamic_cast< CTFWeaponBaseGun* > ( pEntity );
	int iWeaponID = ( pWeapon ) ? pWeapon->GetWeaponID() : TF_WEAPON_NONE;
	CTFWearableDemoShield *pShield = ( pPlayer->IsPlayerClass( TF_CLASS_DEMOMAN ) ) ? dynamic_cast< CTFWearableDemoShield* >( pEntity ) : NULL;
	bool bShield = ( pShield ) ? true : false;
	bool bRocketPack = ( iWeaponID == TF_WEAPON_ROCKETPACK );

	// Hack to simplify excluding non-weapons from damage upgrades
	bool bHideDmgUpgrades = iWeaponID == TF_WEAPON_NONE || 
		iWeaponID == TF_WEAPON_LASER_POINTER || 
		iWeaponID == TF_WEAPON_MEDIGUN || 
		iWeaponID == TF_WEAPON_BUFF_ITEM ||
		iWeaponID == TF_WEAPON_BUILDER ||
		iWeaponID == TF_WEAPON_PDA_ENGINEER_BUILD ||
		iWeaponID == TF_WEAPON_INVIS ||
		iWeaponID == TF_WEAPON_SPELLBOOK ||
		iWeaponID == TF_WEAPON_JAR_GAS ||
		iWeaponID == TF_WEAPON_LUNCHBOX ||
		iWeaponID == TF_WEAPON_JAR ||
		iWeaponID == TF_WEAPON_JAR_MILK ||
		bRocketPack;

	// Prepare for large if statement

	// ---------------------------------------------
	// Misc
	// ---------------------------------------------
	if ( FStrEq( pszGroupName, "damaging" ) )
	{
		return !bHideDmgUpgrades;
	}
	else if ( FStrEq( pszGroupName, "energy" ) )
	{
		return pWeapon && pWeapon->IsEnergyWeapon();
	}
	else if ( FStrEq( pszGroupName, "effect_bar" ) )
	{
		return pWeapon && pWeapon->HasEffectBarRegeneration();
	}
	else if ( FStrEq( pszGroupName, "projectile_penetrate" ) )
	{
		if ( pWeaponGun )
		{
			int iProjectile = pWeaponGun->GetWeaponProjectileType();
			return ( iProjectile == TF_PROJECTILE_ARROW || iProjectile == TF_PROJECTILE_BULLET || iProjectile == TF_PROJECTILE_HEALING_BOLT || iProjectile == TF_PROJECTILE_FESTIVE_ARROW || iProjectile == TF_PROJECTILE_FESTIVE_HEALING_BOLT );
		}
		return false;
	}
	else if ( FStrEq( pszGroupName, "builder" ) )
	{
		return iWeaponID == TF_WEAPON_BUILDER;
	}
	else if ( FStrEq( pszGroupName, "spellbook" ) )
	{
		return iWeaponID == TF_WEAPON_SPELLBOOK;
	}
	else if ( FStrEq( pszGroupName, "jar" ) )
	{
		return ( iWeaponID == TF_WEAPON_JAR || iWeaponID == TF_WEAPON_JAR_MILK );
	}
	else if ( FStrEq( pszGroupName, "single_reload" ) )
	{
		return pWeapon && pWeapon->ReloadsSingly();
	}
	else if ( FStrEq( pszGroupName, "buff_item" ) )
	{
		return ( iWeaponID == TF_WEAPON_BUFF_ITEM );
	}
	else if ( FStrEq( pszGroupName, "meter" ) )
	{
		attrib_value_t eChargeType = ATTRIBUTE_METER_TYPE_NONE;
		CALL_ATTRIB_HOOK_INT_ON_OTHER( pEntity, eChargeType, item_meter_charge_type );
		return eChargeType != ATTRIBUTE_METER_TYPE_NONE;
	}
	else if ( FStrEq( pszGroupName, "parachute" ) )
	{
		return ( iWeaponID == TF_WEAPON_PARACHUTE );
	}

	else if ( FStrEq( pszGroupName, "primary" ) )
	{
		return iWeaponSlot == TF_WPN_TYPE_PRIMARY;
	}
	else if ( FStrEq( pszGroupName, "secondary" ) )
	{
		return iWeaponSlot == TF_WPN_TYPE_SECONDARY;
	}
	else if ( FStrEq( pszGroupName, "melee" ) )
	{
		return dynamic_cast< CTFWeaponBaseMelee* >( pEntity );
	}

	else if ( FStrEq( pszGroupName, "primary_ammo" ) )
	{
		return pWeapon && pWeapon->GetPrimaryAmmoType() == TF_AMMO_PRIMARY;
	}
	else if ( FStrEq( pszGroupName, "secondary_ammo" ) )
	{
		return pWeapon && pWeapon->GetPrimaryAmmoType() == TF_AMMO_SECONDARY;
	}
	else if ( FStrEq( pszGroupName, "clip" ) )
	{
		return pWeapon && !pWeapon->IsBlastImpactWeapon() && pWeapon->UsesClipsForAmmo1() && pWeapon->GetMaxClip1() > 1;
	}
	else if ( FStrEq( pszGroupName, "clip_atomic" ) )
	{
		return pWeapon && pWeapon->IsBlastImpactWeapon();
	}

	// ---------------------------------------------
	// Multiclass Weapons
	// ---------------------------------------------
	else if ( FStrEq( pszGroupName, "shotgun" ) )
	{
		return iWeaponID == TF_WEAPON_SHOTGUN_BUILDING_RESCUE || iWeaponID == TF_WEAPON_SHOTGUN_HWG || iWeaponID == TF_WEAPON_SHOTGUN_PRIMARY || iWeaponID == TF_WEAPON_SHOTGUN_PYRO || iWeaponID == TF_WEAPON_SHOTGUN_SOLDIER || iWeaponID == TF_WEAPON_SENTRY_REVENGE;
	}
	else if ( FStrEq( pszGroupName, "energy_shotgun" ) ) // Bison/pomson
	{
		return iWeaponID == TF_WEAPON_RAYGUN || iWeaponID == TF_WEAPON_DRG_POMSON;
	}
	else if ( FStrEq( pszGroupName, "pistol" ) )
	{
		return iWeaponID == TF_WEAPON_PISTOL || iWeaponID == TF_WEAPON_PISTOL_SCOUT;
	}

	// ---------------------------------------------
	// Scout Weapons
	// ---------------------------------------------
	else if ( FStrEq( pszGroupName, "scattergun" ) )
	{
		return iWeaponID == TF_WEAPON_SCATTERGUN || iWeaponID == TF_WEAPON_HANDGUN_SCOUT_PRIMARY || iWeaponID == TF_WEAPON_SODA_POPPER || iWeaponID == TF_WEAPON_PEP_BRAWLER_BLASTER;
	}
	else if ( FStrEq( pszGroupName, "drink" ) )
	{
		return pPlayer->IsPlayerClass( TF_CLASS_SCOUT ) && iWeaponSlot == TF_WPN_TYPE_SECONDARY && pWeapon && pWeapon->HasEffectBarRegeneration();
	}
	else if ( FStrEq( pszGroupName, "sandman" ) )
	{
		return iWeaponID == TF_WEAPON_BAT_WOOD;
	}
	else if ( FStrEq( pszGroupName, "ball" ) )
	{
		return iWeaponID == TF_WEAPON_BAT_WOOD || iWeaponID == TF_WEAPON_BAT_GIFTWRAP;
	}
	else if ( FStrEq( pszGroupName, "bat" ) )
	{
		return pPlayer->IsPlayerClass( TF_CLASS_SCOUT ) && (iWeaponSlot == TF_WPN_TYPE_MELEE || iWeaponSlot == TF_WPN_TYPE_MELEE_ALLCLASS);
	}

	// ---------------------------------------------
	// Soldier Weapons
	// ---------------------------------------------
	else if ( FStrEq( pszGroupName, "rocket" ) )
	{
		return iWeaponID == TF_WEAPON_ROCKETLAUNCHER || iWeaponID == TF_WEAPON_ROCKETLAUNCHER_DIRECTHIT || iWeaponID == TF_WEAPON_PARTICLE_CANNON;
	}
	else if ( FStrEq( pszGroupName, "shovel" ) )
	{
		return pPlayer->IsPlayerClass( TF_CLASS_SOLDIER ) && (iWeaponSlot == TF_WPN_TYPE_MELEE || iWeaponSlot == TF_WPN_TYPE_MELEE_ALLCLASS);
	}

	// ---------------------------------------------
	// Pyro Weapons
	// ---------------------------------------------
	else if ( FStrEq( pszGroupName, "flamethrower" ) )
	{
		return iWeaponID == TF_WEAPON_FLAMETHROWER;
	}
	else if ( FStrEq( pszGroupName, "dragons_fury" ) )
	{
		return iWeaponID == TF_WEAPON_FLAME_BALL;
	}
	else if ( FStrEq( pszGroupName, "airblast" ) )
	{
		return ( iWeaponID == TF_WEAPON_FLAME_BALL || 
			( iWeaponID == TF_WEAPON_FLAMETHROWER && pWeaponGun && assert_cast< CTFFlameThrower* >( pWeaponGun )->CanAirBlastPushPlayer() ) );
	}
	else if ( FStrEq( pszGroupName, "flare" ) )
	{
		return iWeaponID == TF_WEAPON_FLAREGUN || iWeaponID == TF_WEAPON_FLAREGUN_REVENGE;
	}
	else if ( FStrEq( pszGroupName, "manmelter" ) )
	{
		return iWeaponID == TF_WEAPON_FLAREGUN_REVENGE;
	}
	else if ( FStrEq( pszGroupName, "gas_passer" ) )
	{
		return iWeaponID == TF_WEAPON_JAR_GAS;
	}
	else if ( FStrEq( pszGroupName, "rocket_pack" ) )
	{
		return bRocketPack;
	}
	else if ( FStrEq( pszGroupName, "fireaxe" ) )
	{
		return pPlayer->IsPlayerClass( TF_CLASS_PYRO ) && (iWeaponSlot == TF_WPN_TYPE_MELEE || iWeaponSlot == TF_WPN_TYPE_MELEE_ALLCLASS);
	}

	// ---------------------------------------------
	// Demo Weapons
	// ---------------------------------------------
	else if ( FStrEq( pszGroupName, "grenade" ) )
	{
		return iWeaponID == TF_WEAPON_GRENADELAUNCHER || iWeaponID == TF_WEAPON_CANNON;
	}
	else if ( FStrEq( pszGroupName, "sticky" ) )
	{
		return iWeaponID == TF_WEAPON_PIPEBOMBLAUNCHER;
	}
	else if ( FStrEq( pszGroupName, "shield" ) )
	{
		return bShield;
	}
	else if ( FStrEq( pszGroupName, "sword" ) )
	{
		bool bShieldEquipped = false;
		if ( pPlayer->IsPlayerClass( TF_CLASS_DEMOMAN ) )
		{
			for ( int i = 0; i < pPlayer->GetNumWearables(); ++i )
			{
				CTFWearableDemoShield *pWearableShield = dynamic_cast< CTFWearableDemoShield* >( pPlayer->GetWearable( i ) );
				if ( pWearableShield )
				{
					bShieldEquipped = true;
				}
			}
		}

		return iWeaponID == TF_WEAPON_SWORD && bShieldEquipped;
	}
	else if ( FStrEq( pszGroupName, "bottle" ) )
	{
		return pPlayer->IsPlayerClass( TF_CLASS_DEMOMAN ) && (iWeaponSlot == TF_WPN_TYPE_MELEE || iWeaponSlot == TF_WPN_TYPE_MELEE_ALLCLASS);
	}
	else if ( FStrEq( pszGroupName, "fires_grenade" ) )
	{
		if ( pWeaponGun )
		{
			return ( pWeaponGun->GetWeaponProjectileType() == TF_PROJECTILE_PIPEBOMB );
		}

		return false;
	}

	// ---------------------------------------------
	// Heavy Weapons
	// ---------------------------------------------
	else if ( FStrEq( pszGroupName, "minigun" ) )
	{
		return iWeaponID == TF_WEAPON_MINIGUN;
	}
	else if ( FStrEq( pszGroupName, "fists" ) )
	{
		return pPlayer->IsPlayerClass( TF_CLASS_HEAVYWEAPONS ) && (iWeaponSlot == TF_WPN_TYPE_MELEE || iWeaponSlot == TF_WPN_TYPE_MELEE_ALLCLASS);
	}

	// ---------------------------------------------
	// Engineer Weapons
	// ---------------------------------------------
	else if ( FStrEq( pszGroupName, "build_pda" ) )
	{
		return iWeaponID == TF_WEAPON_PDA_ENGINEER_BUILD;
	}
	else if ( FStrEq( pszGroupName, "wrench" ) )
	{
		return pPlayer->IsPlayerClass( TF_CLASS_ENGINEER ) && (iWeaponSlot == TF_WPN_TYPE_MELEE || iWeaponSlot == TF_WPN_TYPE_MELEE_ALLCLASS);
	}

	// ---------------------------------------------
	// Medic Weapons
	// ---------------------------------------------
	else if ( FStrEq( pszGroupName, "syringe" ) )
	{
		return iWeaponID == TF_WEAPON_SYRINGEGUN_MEDIC;
	}
	else if ( FStrEq( pszGroupName, "medigun" ) )
	{
		return iWeaponID == TF_WEAPON_MEDIGUN;
	}
	else if ( FStrEq( pszGroupName, "bonesaw" ) )
	{
		return pPlayer->IsPlayerClass( TF_CLASS_MEDIC ) && (iWeaponSlot == TF_WPN_TYPE_MELEE || iWeaponSlot == TF_WPN_TYPE_MELEE_ALLCLASS);
	}

	// ---------------------------------------------
	// Sniper Weapons
	// ---------------------------------------------
	else if ( FStrEq( pszGroupName, "rifle" ) )
	{
		return WeaponID_IsSniperRifle( iWeaponID );
	}
	else if ( FStrEq( pszGroupName, "bow" ) )
	{
		return iWeaponID == TF_WEAPON_COMPOUND_BOW;
	}
	else if ( FStrEq( pszGroupName, "kukri" ) )
	{
		return pPlayer->IsPlayerClass( TF_CLASS_SNIPER ) && (iWeaponSlot == TF_WPN_TYPE_MELEE || iWeaponSlot == TF_WPN_TYPE_MELEE_ALLCLASS);
	}

	// ---------------------------------------------
	// Spy Weapons
	// ---------------------------------------------
	else if ( FStrEq( pszGroupName, "sapper" ) )
	{
		return ( pPlayer->IsPlayerClass( TF_CLASS_SPY ) && iWeaponID == TF_WEAPON_BUILDER );
	}
	else if ( FStrEq( pszGroupName, "knife" ) )
	{
		return pPlayer->IsPlayerClass( TF_CLASS_SPY ) && (iWeaponSlot == TF_WPN_TYPE_MELEE || iWeaponSlot == TF_WPN_TYPE_MELEE_ALLCLASS);
	}

	return false;
}

//-----------------------------------------------------------------------------
// Purpose: 
//-----------------------------------------------------------------------------
int CMannVsMachineUpgradeManager::GetAttributeIndexByName( const char* pszAttributeName )
{
	// Already in the map?
	if( m_AttribMap.Find( pszAttributeName ) != m_AttribMap.InvalidIndex() )
	{
		return m_AttribMap.Element( m_AttribMap.Find( pszAttributeName ) );
	}

	// Not in the map.  Find it in the vector and add it to the map
	for( int i=0, nCount = m_Upgrades.Count() ; i<nCount; ++i )
	{
		// Find the index
		const char* pszAttrib = m_Upgrades[i].szAttrib;
		if( FStrEq( pszAttributeName, pszAttrib ) )
		{
			// Add to map
			m_AttribMap.Insert( pszAttributeName, i );
			// Return value
			return i;
		}
	}

	AssertMsg1( 0, "Attribute \"%s\" not found!", pszAttributeName );
	return -1;
}

//-----------------------------------------------------------------------------
// Purpose: 
//-----------------------------------------------------------------------------
void CMannVsMachineUpgradeManager::LoadUpgradesFile( void )
{
	// Determine the upgrades file to load
	const char *pszPath = "scripts/items/mvm_upgrades.txt";

	// Allow map to override
	const char *pszCustomUpgradesFile = TFGameRules()->GetCustomUpgradesFile();
	if ( TFGameRules() && pszCustomUpgradesFile && pszCustomUpgradesFile[0] )
	{
		pszPath = pszCustomUpgradesFile;
	}

	LoadUpgradesFileFromPath( pszPath );
}


//-----------------------------------------------------------------------------
// Purpose: Loads an upgrade file from a specific path
//-----------------------------------------------------------------------------
void CMannVsMachineUpgradeManager::LoadUpgradesFileFromPath( const char *pszPath )
{
	// Check that the path is valid
	const char *pszExtension = V_GetFileExtension( pszPath );
	if ( V_strstr( pszPath, ".." ) || V_strstr( pszPath, " " ) ||
		V_strstr( pszPath, "\r" ) || V_strstr( pszPath, "\n" ) ||
		V_strstr( pszPath, ":" ) || V_strstr( pszPath, "\\\\" ) ||
		V_IsAbsolutePath( pszPath ) ||
		pszExtension == NULL || V_strcmp( pszExtension, "txt" ) != 0 )
	{
		return;
	}

	KeyValues *pKV = new KeyValues( "Upgrades" );
	if ( !pKV->LoadFromFile( filesystem, pszPath, "GAME" ) )
	{
		Warning( "Can't open %s\n", pszPath );
		pKV->deleteThis();
		return;
	}

	m_Upgrades.RemoveAll();
	m_UpgradeGroups.RemoveAll();

	// Parse upgrades.txt
	ParseUpgradeBlockForUIGroup( pKV->FindKey( "ItemUpgrades" ), 0 );
	ParseUpgradeBlockForUIGroup( pKV->FindKey( "PlayerUpgrades" ), 1 );
	ParseUpgradeGroupBlock( pKV->FindKey( "UpgradeGroups" ) );

	pKV->deleteThis();
}



int GetUpgradeStepData( CTFPlayer *pPlayer, int nWeaponSlot, int nUpgradeIndex, int &nCurrentStep, bool &bOverCap )
{
	if ( !pPlayer )
		return 0;

	// Get the item entity. We use the entity, not the item in the loadout, because we want
	// the dynamic attributes that have already been purchases and attached.
	CEconEntity *pEntity = NULL;
	const CEconItemView *pItemData = CTFPlayerSharedUtils::GetEconItemViewByLoadoutSlot( pPlayer, nWeaponSlot, &pEntity );

	const CMannVsMachineUpgrades *pMannVsMachineUpgrade = &( g_MannVsMachineUpgrades.m_Upgrades[ nUpgradeIndex ] );

	CEconItemAttributeDefinition *pAttribDef = ItemSystem()->GetStaticDataForAttributeByName( pMannVsMachineUpgrade->szAttrib );
	if ( !pAttribDef )
		return 0;

	// Special-case short-circuit logic for the powerup bottle. I don't know why we do this, but
	// we did before so this seems like the safest way of not breaking anything.
	const CTFPowerupBottle *pPowerupBottle = dynamic_cast< CTFPowerupBottle* >( pEntity );
	if ( pPowerupBottle )
	{
		Assert( pMannVsMachineUpgrade->nUIGroup == UIGROUP_POWERUPBOTTLE );

		nCurrentStep = ::FindAttribute( pItemData, pAttribDef )
					 ? pPowerupBottle->GetNumCharges()
					 : 0;
		bOverCap = nCurrentStep == pPowerupBottle->GetMaxNumCharges();
		
		return pPowerupBottle->GetMaxNumCharges();
	}

	Assert( pAttribDef->IsStoredAsFloat() );
	Assert( !pAttribDef->IsStoredAsInteger() );

	int nFormat = pAttribDef->GetDescriptionFormat();

	bool bPercentage = nFormat == ATTDESCFORM_VALUE_IS_PERCENTAGE || nFormat == ATTDESCFORM_VALUE_IS_INVERTED_PERCENTAGE;

	// Find the baseline value for this attribute. We start by assuming that it has no default value on
	// the item level (CEconItem) and defaulting to 100% for percentages and 0 for anything else.
	float flBase = bPercentage ? 1.0f : 0.0f;
	
	// If the item has a backing store, we pull from that to find the attribute value before any
	// gameplay-specific (CEconItemView-level) attribute modifications. If we're a player we don't have
	// any persistent backing store. This will either stomp our above value if found or leave it unchanged
	// if not found.
	if ( pItemData && pItemData->GetSOCData() )
	{
		::FindAttribute_UnsafeBitwiseCast<attrib_value_t>( pItemData->GetSOCData(), pAttribDef, &flBase );
	}

	// ...
	float flCurrentAttribValue = bPercentage ? 1.0f : 0.0f;
	
	if ( pMannVsMachineUpgrade->nUIGroup == UIGROUP_UPGRADE_ATTACHED_TO_PLAYER )
	{
		::FindAttribute_UnsafeBitwiseCast<attrib_value_t>( pPlayer->GetAttributeList(), pAttribDef, &flCurrentAttribValue );
	}
	else
	{
		Assert( pMannVsMachineUpgrade->nUIGroup == UIGROUP_UPGRADE_ATTACHED_TO_ITEM );
		if ( pItemData )
		{
			::FindAttribute_UnsafeBitwiseCast<attrib_value_t>( pItemData, pAttribDef, &flCurrentAttribValue );
		}
	}

	// ...
	const float flIncrement = GetIncrement( pMannVsMachineUpgrade->flIncrement, pMannVsMachineUpgrade->flCap, pMannVsMachineUpgrade->flMult, nFormat );//pMannVsMachineUpgrade->flIncrement;
	
	// Figure out the cap value for this attribute. We start by trusting whatever is specified in our
	// upgrade config but if we're dealing with an item that specifies different properties at a level
	// before MvM upgrades (ie., the Soda Popper already specifies "Reload time decreased") then we
	// need to make sure we consider that the actual high end for UI purposes.
	const float flCap = GetCap( pMannVsMachineUpgrade->flIncrement, pMannVsMachineUpgrade->flCap, pMannVsMachineUpgrade->flMult, nFormat );//pMannVsMachineUpgrade->flCap;

	if ( BIsAttributeValueWithDeltaOverCap( flCurrentAttribValue, flIncrement, flCap ) )
	{
		// Early out here -- we know we're over the cap already, so just fill out and return values
		// that show that.
		bOverCap = true;
		nCurrentStep = RoundFloatToInt( fabsf( ( flCurrentAttribValue - flBase ) / flIncrement ) );

		return nCurrentStep;			// Include the 0th step
	}

	// Calculate the the total number of upgrade levels and current upgrade level
	int nNumSteps = 0;
	
	// ...
	nNumSteps = RoundFloatToInt( fabsf( ( flCap - flBase ) / flIncrement ) );
	nCurrentStep = RoundFloatToInt( fabsf( ( flCurrentAttribValue - flBase ) / flIncrement ) );

	// Include the 0th step
	return nNumSteps;
}

float GetIncrement( float flIncrement, float flCap, float flMult, int nFormat )
{
	if ( nFormat == ATTDESCFORM_VALUE_IS_INVERTED_PERCENTAGE )
	{
		return -(1.f - GetCap( flIncrement, flCap, flMult, nFormat )) / ((1.f - flCap) / -flIncrement);
	}
	else
	{
		return flIncrement * RemapValClamped( flMult, 0.f, 1.f, 1.f, tf_mvm_upgrade_mult.GetFloat() );
	}
}

float GetCap( float flIncrement, float flCap, float flMult, int nFormat )
{
	if ( nFormat == ATTDESCFORM_VALUE_IS_INVERTED_PERCENTAGE )
	{
		return flCap / RemapValClamped( flMult, 0.f, 1.f, 1.f, tf_mvm_upgrade_mult.GetFloat() );
	}
	else if ( nFormat == ATTDESCFORM_VALUE_IS_PERCENTAGE )
	{
		return (flCap - 1.f) * RemapValClamped( flMult, 0.f, 1.f, 1.f, tf_mvm_upgrade_mult.GetFloat() ) + 1.f;
	}
	else
	{
		return flCap * RemapValClamped( flMult, 0.f, 1.f, 1.f, tf_mvm_upgrade_mult.GetFloat() );
	}
}