#include "Precompile.h"
#include "DependencyLoader.h"
#include "DependencyCollection.h"

#include "Browser.h"

using namespace Luna;


///////////////////////////////////////////////////////////////////////////////
/// class DependencyLoader
///////////////////////////////////////////////////////////////////////////////
DependencyLoader::DependencyLoader( const std::string& rootDirectory, const std::string& configDirectory, DependencyCollection* collection )
: Nocturnal::ThreadMechanism( "DependencyLoader" )
, m_RootDirectory( rootDirectory )
, m_ConfigDirectory( configDirectory )
, m_Collection( collection )
{
}

///////////////////////////////////////////////////////////////////////////////
DependencyLoader::~DependencyLoader()
{
    m_AssetPaths.clear();
}

///////////////////////////////////////////////////////////////////////////////
void DependencyLoader::InitData()
{
    m_AssetPaths.clear();
}

///////////////////////////////////////////////////////////////////////////////
void DependencyLoader::ThreadProc( i32 threadID )
{
    ThreadEnter( threadID );

    Nocturnal::Path cacheDBFilepath( m_RootDirectory + "/.tracker/cache.db" );
    Asset::CacheDBPtr cacheDB = new Asset::CacheDB( "LunaBrowserDependencyLoader-AssetCacheDB", cacheDBFilepath.Get(), m_ConfigDirectory );

    cacheDB->GetAssetDependencies( m_Collection->GetRoot(), m_AssetPaths, m_Collection->IsReverse(), m_Collection->GetRecursionDepthForLoad(), 0, &m_StopThread );

    if ( CheckThreadLeave( threadID ) )
    {
        return;
    }

    m_Collection->SetAssetReferences( m_AssetPaths );

    ThreadLeave( threadID );
}

///////////////////////////////////////////////////////////////////////////////
void DependencyLoader::OnEndThread( const Nocturnal::ThreadProcArgs& args )
{
    if ( !IsCurrentThread( args.m_ThreadID ) )
        return;

    m_Collection->IsLoading( false );
    m_Collection->Thaw();
}