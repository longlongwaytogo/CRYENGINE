// Copyright 2001-2016 Crytek GmbH / Crytek Group. All rights reserved.

#include "StdAfx.h"
#include "AudioAssets.h"
#include "AudioAssetsManager.h"
#include "IEditor.h"
#include <IAudioSystemItem.h>
#include <ACETypes.h>
#include "AudioControlsEditorPlugin.h"
#include "ImplementationManager.h"
#include <CrySerialization/StringList.h>
#include <CrySerialization/Decorators/Range.h>
#include <Serialization/Decorators/EditorActionButton.h>
#include <Serialization/Decorators/ToggleButton.h>
#include <CryMath/Cry_Geo.h>
#include <Util/Math.h>
#include <ConfigurationManager.h>

namespace ACE
{

void IAudioAsset::SetParent(IAudioAsset* pParent)
{
	m_pParent = pParent;
	SetModified(true);
}

void IAudioAsset::AddChild(IAudioAsset* pChildControl)
{
	m_children.push_back(pChildControl);
	SetModified(true);
}

void IAudioAsset::RemoveChild(IAudioAsset* pChildControl)
{
	m_children.erase(std::remove(m_children.begin(), m_children.end(), pChildControl), m_children.end());
	SetModified(true);
}

CAudioControl::CAudioControl(const string& controlName, CID id, EItemType type, CAudioAssetsManager* pAssetsManager)
	: IAudioAsset(controlName)
	, m_id(id)
	, m_type(type)
	, m_pAssetsManager(pAssetsManager)
{
	m_modifiedSignalEnabled = false;
	m_scope = Utils::GetGlobalScope();
	m_modifiedSignalEnabled = true;
}

CAudioControl::~CAudioControl()
{
	m_connectedControls.clear();
}

CID CAudioControl::GetId() const
{
	return m_id;
}

void CAudioControl::SetName(const string& name)
{
	if (name != m_name)
	{
		SignalControlAboutToBeModified();
		m_name = Utils::GenerateUniqueControlName(name, GetType(), *m_pAssetsManager);
		SignalControlModified();
		SetModified(true);
	}
}

Scope CAudioControl::GetScope() const
{
	return m_scope;
}

void CAudioControl::SetScope(Scope scope)
{
	if (m_scope != scope)
	{
		SignalControlAboutToBeModified();
		m_scope = scope;
		SignalControlModified();
		SetModified(true);
	}
}

bool CAudioControl::IsAutoLoad() const
{
	return m_bAutoLoad;
}

void CAudioControl::SetAutoLoad(bool bAutoLoad)
{
	if (bAutoLoad != m_bAutoLoad)
	{
		SignalControlAboutToBeModified();
		m_bAutoLoad = bAutoLoad;
		SignalControlModified();
		SetModified(true);
	}
}

size_t CAudioControl::GetConnectionCount()
{
	return m_connectedControls.size();
}

ConnectionPtr CAudioControl::GetConnectionAt(int index)
{
	if (index < m_connectedControls.size())
	{
		return m_connectedControls[index];
	}
	return nullptr;
}

ConnectionPtr CAudioControl::GetConnection(CID id)
{
	if (id >= 0)
	{
		const size_t size = m_connectedControls.size();
		for (int i = 0; i < size; ++i)
		{
			ConnectionPtr pConnection = m_connectedControls[i];
			if (pConnection && pConnection->GetID() == id)
			{
				return pConnection;
			}
		}
	}
	return nullptr;
}

ConnectionPtr CAudioControl::GetConnection(IAudioSystemItem* pAudioSystemControl)
{
	return GetConnection(pAudioSystemControl->GetId());
}

void CAudioControl::AddConnection(ConnectionPtr pConnection)
{
	if (pConnection)
	{
		m_connectedControls.push_back(pConnection);

		if (m_bMatchRadiusAndAttenuation)
		{
			MatchRadiusToAttenuation();
		}

		pConnection->signalConnectionChanged.Connect(this, &CAudioControl::SignalControlModified);

		IAudioSystemEditor* pAudioSystemImpl = CAudioControlsEditorPlugin::GetImplementationManger()->GetImplementation();
		if (pAudioSystemImpl)
		{
			IAudioSystemItem* pAudioSystemControl = pAudioSystemImpl->GetControl(pConnection->GetID());
			if (pAudioSystemControl)
			{
				pAudioSystemImpl->EnableConnection(pConnection);
				SignalConnectionAdded(pAudioSystemControl);
			}
		}

		SignalControlModified();
		SetModified(true);
	}
}

void CAudioControl::RemoveConnection(ConnectionPtr pConnection)
{
	if (pConnection)
	{
		auto it = std::find(m_connectedControls.begin(), m_connectedControls.end(), pConnection);
		if (it != m_connectedControls.end())
		{
			IAudioSystemEditor* pAudioSystemImpl = CAudioControlsEditorPlugin::GetImplementationManger()->GetImplementation();
			if (pAudioSystemImpl)
			{
				IAudioSystemItem* pAudioSystemControl = pAudioSystemImpl->GetControl(pConnection->GetID());
				if (pAudioSystemControl)
				{

					pAudioSystemImpl->DisableConnection(pConnection);
					m_connectedControls.erase(it);

					if (m_bMatchRadiusAndAttenuation)
					{
						MatchRadiusToAttenuation();
					}

					SignalConnectionRemoved(pAudioSystemControl);
					SignalControlModified();
					SetModified(true);
				}
			}
		}
	}
}

void CAudioControl::ClearConnections()
{
	if (!m_connectedControls.empty())
	{
		IAudioSystemEditor* pAudioSystemImpl = CAudioControlsEditorPlugin::GetImplementationManger()->GetImplementation();
		if (pAudioSystemImpl)
		{
			for (ConnectionPtr& connection : m_connectedControls)
			{
				pAudioSystemImpl->DisableConnection(connection);
				IAudioSystemItem* pAudioSystemControl = pAudioSystemImpl->GetControl(connection->GetID());
				if (pAudioSystemControl)
				{
					SignalConnectionRemoved(pAudioSystemControl);
				}
			}
		}
		m_connectedControls.clear();

		if (m_bMatchRadiusAndAttenuation)
		{
			MatchRadiusToAttenuation();
		}
		SignalControlModified();
		SetModified(true);
	}
}

void CAudioControl::RemoveConnection(IAudioSystemItem* pAudioSystemControl)
{
	if (pAudioSystemControl)
	{
		const CID id = pAudioSystemControl->GetId();
		auto it = m_connectedControls.begin();
		auto end = m_connectedControls.end();
		for (; it != end; ++it)
		{
			if ((*it)->GetID() == id)
			{
				IAudioSystemEditor* pAudioSystemImpl = CAudioControlsEditorPlugin::GetImplementationManger()->GetImplementation();
				if (pAudioSystemImpl)
				{
					pAudioSystemImpl->DisableConnection(*it);
				}
				m_connectedControls.erase(it);

				if (m_bMatchRadiusAndAttenuation)
				{
					MatchRadiusToAttenuation();
				}

				SignalConnectionRemoved(pAudioSystemControl);
				SignalControlModified();
				SetModified(true);
				return;
			}
		}
	}
}

void CAudioControl::SignalControlModified()
{
	if (m_modifiedSignalEnabled && m_pAssetsManager)
	{
		m_pAssetsManager->OnControlModified(this);
	}
}

void CAudioControl::SignalControlAboutToBeModified()
{
	if (m_modifiedSignalEnabled && m_pAssetsManager)
	{
		m_pAssetsManager->OnControlAboutToBeModified(this);
	}
}

void CAudioControl::SignalConnectionAdded(IAudioSystemItem* pMiddlewareControl)
{
	if (m_pAssetsManager)
	{
		m_pAssetsManager->OnConnectionAdded(this, pMiddlewareControl);
	}
}

void CAudioControl::SignalConnectionRemoved(IAudioSystemItem* pMiddlewareControl)
{
	if (m_pAssetsManager)
	{
		m_pAssetsManager->OnConnectionRemoved(this, pMiddlewareControl);
	}
}

void CAudioControl::ReloadConnections()
{
	IAudioSystemEditor* pAudioSystemImpl = CAudioControlsEditorPlugin::GetImplementationManger()->GetImplementation();
	if (pAudioSystemImpl)
	{
		std::map<int, XMLNodeList> connectionNodes;
		std::swap(connectionNodes, m_connectionNodes);
		for (auto& connectionPair : connectionNodes)
		{
			for (auto& connection : connectionPair.second)
			{
				LoadConnectionFromXML(connection.xmlNode, connectionPair.first);
			}
		}
	}
}

void CAudioControl::LoadConnectionFromXML(XmlNodeRef xmlNode, int platformIndex)
{
	IAudioSystemEditor* pAudioSystemImpl = CAudioControlsEditorPlugin::GetImplementationManger()->GetImplementation();
	if (pAudioSystemImpl)
	{
		ConnectionPtr pConnection = pAudioSystemImpl->CreateConnectionFromXMLNode(xmlNode, GetType());
		if (pConnection)
		{
			if (GetType() == eItemType_Preload)
			{
				// The connection could already exist but using a different platform
				ConnectionPtr pPreviousConnection = GetConnection(pConnection->GetID());
				if (pPreviousConnection == nullptr)
				{
					if (platformIndex != -1)
					{
						pConnection->ClearPlatforms();
					}
					AddConnection(pConnection);
				}
				else
				{
					pConnection = pPreviousConnection;
				}
				if (platformIndex != -1)
				{
					pConnection->EnableForPlatform(platformIndex, true);
				}
			}
			else
			{
				AddConnection(pConnection);
			}
		}
		else if (GetType() == eItemType_Preload && platformIndex == -1)
		{
			// If it's a preload connection from another middleware and the platform
			// wasn't found (old file format) fall back to adding them to all the platforms
			const std::vector<dll_string>& platforms = GetIEditor()->GetConfigurationManager()->GetPlatformNames();
			const size_t count = platforms.size();
			for (size_t i = 0; i < count; ++i)
			{
				AddRawXMLConnection(xmlNode, false, i);
			}
			return;
		}

		AddRawXMLConnection(xmlNode, pConnection != nullptr, platformIndex);
	}
}

void CAudioControl::Serialize(Serialization::IArchive& ar)
{
	if (ar.openBlock("properties", "+Properties"))
	{
		// Name
		string newName = m_name;
		ar(newName, "name", "Name");

		// Scope
		Serialization::StringList scopeList;
		ScopeInfoList scopeInfoList;
		m_pAssetsManager->GetScopeInfoList(scopeInfoList);
		for (auto& scope : scopeInfoList)
		{
			scopeList.push_back(scope.name);
		}
		Serialization::StringListValue selectedScope(scopeList, m_pAssetsManager->GetScopeInfo(m_scope).name);
		Scope newScope = m_scope;
		if (m_type != eItemType_State)
		{
			ar(selectedScope, "scope", "Scope");
			newScope = m_pAssetsManager->GetScope(scopeList[selectedScope.index()]);
		}

		// Auto Load
		bool bAutoLoad = m_bAutoLoad;
		if (m_type == eItemType_Preload)
		{
			ar(bAutoLoad, "auto_load", "Auto Load");
		}

		// Max Radius
		float maxRadius = m_radius;
		float fadeOutDistance = m_occlusionFadeOutDistance;
		if (m_type == eItemType_Trigger)
		{

			if (ar.openBlock("activity_radius", "Activity Radius"))
			{
				bool hasPlaceholderConnections = false;
				float attenuationRadius = 0.0f;
				IAudioSystemEditor* pAudioSystemImpl = CAudioControlsEditorPlugin::GetImplementationManger()->GetImplementation();
				if (pAudioSystemImpl)
				{
					float radius = 0.0f;
					for (auto pConnection : m_connectedControls)
					{
						IAudioSystemItem* pItem = pAudioSystemImpl->GetControl(pConnection->GetID());
						if (pItem && !pItem->IsPlaceholder())
						{
							attenuationRadius = std::max(attenuationRadius, pItem->GetRadius());
						}
						else
						{
							// If control has placeholder connection we cannot enforce the link between activity radius
							// and attenuation as the user could be missing the middleware project
							hasPlaceholderConnections = true;
						}
					}
				}

				if (m_bMatchRadiusAndAttenuation && !hasPlaceholderConnections)
				{
					ar(Serialization::Range<float>(maxRadius, 0.0f, std::numeric_limits<float>::max()), "max_radius", "!^");
				}
				else
				{
					ar(Serialization::Range<float>(maxRadius, 0.0f, std::numeric_limits<float>::max()), "max_radius", "^");
				}

				if (!hasPlaceholderConnections)
				{
					if (ar.openBlock("attenuation", "Attenuation"))
					{
						ar(attenuationRadius, "attenuation", "!^");
						ar(Serialization::ToggleButton(m_bMatchRadiusAndAttenuation, "icons:Navigation/Tools_Link.ico", "icons:Navigation/Tools_Link_Unlink.ico"), "link", "^");
						if (m_bMatchRadiusAndAttenuation)
						{
							maxRadius = attenuationRadius;
						}

						ar.closeBlock();
					}
				}
				ar(Serialization::Range<float>(fadeOutDistance, 0.0f, maxRadius), "fadeOutDistance", "Occlusion Fade-Out Distance");
			}
		}

		if (ar.isInput())
		{
			SignalControlAboutToBeModified();
			m_modifiedSignalEnabled = false; // we are manually sending the signals and don't want the other SetX functions to send anymore
			SetName(newName);
			SetScope(newScope);
			SetAutoLoad(bAutoLoad);
			SetRadius(maxRadius);
			SetOcclusionFadeOutDistance(fadeOutDistance);
			m_modifiedSignalEnabled = true;
			SetModified(true);
			SignalControlModified();
		}
	}

}

bool CAudioControl::IsModified() const
{
	if (m_pParent)
	{
		return m_pParent->IsModified();
	}
	return false;
}

void CAudioControl::SetRadius(float radius)
{
	if (radius != m_radius)
	{
		SignalControlAboutToBeModified();
		m_radius = radius;
		SignalControlModified();
	}
}

void CAudioControl::SetOcclusionFadeOutDistance(float fadeOutDistance)
{
	if (fadeOutDistance != m_occlusionFadeOutDistance)
	{
		SignalControlAboutToBeModified();
		m_occlusionFadeOutDistance = fadeOutDistance;
		SignalControlModified();
	}
}

void CAudioControl::SetModified(bool bModified)
{
	if (m_pParent)
	{
		m_pParent->SetModified(bModified);
	}
}

void CAudioControl::AddRawXMLConnection(XmlNodeRef xmlNode, bool bValid, int platformIndex /*= -1*/)
{
	m_connectionNodes[platformIndex].push_back(SRawConnectionData(xmlNode, bValid));
}

XMLNodeList& CAudioControl::GetRawXMLConnections(int platformIndex /*= -1*/)
{
	return m_connectionNodes[platformIndex];
}

void CAudioControl::MatchRadiusToAttenuation()
{
	IAudioSystemEditor* pAudioSystemImpl = CAudioControlsEditorPlugin::GetImplementationManger()->GetImplementation();
	if (pAudioSystemImpl)
	{
		float radius = 0.0f;
		for (auto pConnection : m_connectedControls)
		{
			IAudioSystemItem* pItem = pAudioSystemImpl->GetControl(pConnection->GetID());
			if (pItem)
			{
				if (pItem->IsPlaceholder())
				{
					// We don't match controls that have placeholder
					// connections as we don't know what the real values should be
					return;
				}
				else
				{
					radius = std::max(radius, pItem->GetRadius());
				}
			}
		}

		SetRadius(radius);
	}
}

bool CAudioFolder::IsModified() const
{
	if (m_pParent)
	{
		return m_pParent->IsModified();
	}
	return false;
}

void CAudioFolder::SetModified(bool bModified)
{
	if (m_pParent)
	{
		m_pParent->SetModified(bModified);
	}
}

}