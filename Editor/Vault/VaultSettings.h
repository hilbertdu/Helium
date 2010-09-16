#pragma once

#include "VaultMenuIDs.h"
#include "VaultSearchQuery.h"

#include "Foundation/TUID.h"
#include "Foundation/Container/OrderedSet.h"
#include "Editor/WindowSettings.h"

namespace Helium
{
    namespace Editor
    {
        class VaultPanel;
        class VaultSettings : public Reflect::ConcreteInheritor< VaultSettings, Reflect::Element >
        {
        public:
            VaultSettings( const tstring& defaultFolder = TXT( "" ),
                VaultViewMode viewVaultMode = VaultViewModes::Details,
                u32 thumbnailSize = VaultThumbnailsSizes::Medium );
            ~VaultSettings();

            void GetWindowSettings( VaultPanel* vaultPanel, wxAuiManager* manager = NULL );
            void SetWindowSettings( VaultPanel* vaultPanel, wxAuiManager* manager = NULL );

            const VaultViewMode GetThumbnailMode() const;
            void SetViewMode( VaultViewMode vaultViewMode );

            const u32 GetThumbnailSize() const;
            void SetThumbnailSize( u32 thumbnailSize );

            bool DisplayPreviewAxis() const;
            void SetDisplayPreviewAxis( bool display );
            const Reflect::Field* DisplayPreviewAxisField() const;

            const tstring& GetDefaultFolderPath() const;
            void SetDefaultFolderPath( const tstring& path );

        private:
            WindowSettingsPtr     m_WindowSettings;
            //tstring               m_DefaultFolder;
            VaultViewMode         m_VaultViewMode;
            u32                   m_ThumbnailSize;
            bool                  m_DisplayPreviewAxis;

        public:
            static void EnumerateClass( Reflect::Compositor< VaultSettings >& comp )
            {
                comp.AddField( &VaultSettings::m_WindowSettings, "m_WindowSettings", Reflect::FieldFlags::Hide );
                //comp.AddField( &VaultSettings::m_DefaultFolder, "m_DefaultFolder", Reflect::FieldFlags::Hide );
                comp.AddEnumerationField( &VaultSettings::m_VaultViewMode, "m_VaultViewMode" );

                {
                    Reflect::Field* field = comp.AddField( &VaultSettings::m_ThumbnailSize, "m_ThumbnailSize" );
                    field->SetProperty( TXT( "UIScript" ), TXT( "UI[.[slider{min=16.0; max=256.0} value{}].]" ) );
                }

                comp.AddField( &VaultSettings::m_DisplayPreviewAxis, "m_DisplayPreviewAxis" );
            }
        };

        typedef Helium::SmartPtr< VaultSettings > VaultSettingsPtr;
    }
}