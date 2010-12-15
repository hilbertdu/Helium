#include "ArchiveBinary.h"
#include "Element.h"
#include "Registry.h"
#include "Foundation/Reflect/Data/DataDeduction.h"

#include "Foundation/SmartBuffer/SmartBuffer.h"
#include "Foundation/Container/Insert.h" 
#include "Foundation/Checksum/CRC32.h"
#include "Foundation/Memory/Endian.h"

using Helium::Insert;
using namespace Helium;
using namespace Helium::Reflect; 

//#define REFLECT_DEBUG_BINARY_CRC
//#define REFLECT_DISABLE_BINARY_CRC

// version / feature management 
const uint32_t ArchiveBinary::CURRENT_VERSION = 7;

// CRC
const uint32_t CRC_DEFAULT = 0x10101010;
const uint32_t CRC_INVALID = 0xffffffff;

#ifdef REFLECT_DEBUG_BINARY_CRC
const uint32_t CRC_BLOCK_SIZE = 4;
#else
const uint32_t CRC_BLOCK_SIZE = 4096;
#endif

// this is sneaky, but in general people shouldn't use this
namespace Helium
{
    namespace Reflect
    {
        FOUNDATION_API bool g_OverrideCRC = false;
    }
}

//
// Binary Archive implements our own custom serialization technique
//

ArchiveBinary::ArchiveBinary( const Path& path, ByteOrder byteOrder )
: Archive( path, byteOrder )
, m_Version( CURRENT_VERSION )
, m_Size( 0 )
, m_Skip( false )
{
}

ArchiveBinary::ArchiveBinary()
: Archive()
, m_Version( CURRENT_VERSION )
, m_Size( 0 )
, m_Skip( false )
{
}

void ArchiveBinary::Open( bool write )
{
#ifdef REFLECT_ARCHIVE_VERBOSE
    Log::Debug(TXT("Opening file '%s'\n"), m_Path.c_str());
#endif

    Reflect::CharStreamPtr stream = new FileStream<char>( m_Path, write, m_ByteOrder ); 
    OpenStream( stream, write );
}

void ArchiveBinary::OpenStream( CharStream* stream, bool write )
{
    // save the mode here, so that we safely refer to it later.
    m_Mode = (write) ? ArchiveModes::Write : ArchiveModes::Read; 

    // open the stream, this is "our interface" 
    stream->Open(); 

    // Set precision
    stream->SetPrecision(32);

    // Setup stream
    m_Stream = stream; 
}

void ArchiveBinary::Close()
{
    HELIUM_ASSERT( m_Stream );

    m_Stream->Close(); 
    m_Stream = NULL; 
}

void ArchiveBinary::Read()
{
    REFLECT_SCOPE_TIMER( ("Reflect - Binary Read") );

    StatusInfo info( *this, ArchiveStates::Starting );
    e_Status.Raise( info );

    m_Abort = false;

    // determine the size of the input stream
    m_Stream->SeekRead(0, std::ios_base::end);
    m_Size = (long) m_Stream->TellRead();
    m_Stream->SeekRead(0, std::ios_base::beg);

    // fail on an empty input stream
    if ( m_Size == 0 )
    {
        throw Reflect::StreamException( TXT( "Input stream is empty" ) );
    }

    // setup visitors
    PreDeserialize();

    // read byte order
    ByteOrder byteOrder = Helium::PlatformByteOrder;
    uint16_t byteOrderMarker = 0;
    m_Stream->Read( &byteOrderMarker );
    switch ( byteOrderMarker )
    {
    case 0xfeff:
        byteOrder = Helium::PlatformByteOrder;
        break;
    case 0xfffe:
        switch ( Helium::PlatformByteOrder )
        {
        case ByteOrders::LittleEndian:
            byteOrder = ByteOrders::BigEndian;
            break;
        case ByteOrders::BigEndian:
            byteOrder = ByteOrders::LittleEndian;
            break;
        }
        break;
    default:
        throw Helium::Exception( TXT( "Unknown byte order read from file: %s" ), m_Path.c_str() );
    }

    // read character encoding
    CharacterEncoding encoding = CharacterEncodings::ASCII;
    uint8_t encodingByte;
    m_Stream->Read(&encodingByte);
    encoding = (CharacterEncoding)encodingByte;
    if ( encoding != CharacterEncodings::ASCII && encoding != CharacterEncodings::UTF_16 )
    {
        throw Reflect::StreamException( TXT( "Input stream contains an unknown character encoding: %d\n" ), encoding); 
    }

    // read version
    m_Stream->Read(&m_Version);

    if (m_Version > CURRENT_VERSION)
    {
        throw Reflect::StreamException( TXT( "Input stream version is higher than what is supported (input: %d, current: %d)\n" ), m_Version, CURRENT_VERSION); 
    }

    // read and verify CRC
    uint32_t crc = CRC_DEFAULT;
    uint32_t current_crc = Helium::BeginCrc32();
    m_Stream->Read(&crc); 

#ifdef REFLECT_DISABLE_BINARY_CRC
    crc = CRC_DEFAULT;
#endif

    // snapshot our starting location
    uint32_t start = (uint32_t)m_Stream->TellRead();

    // if we are not the stub
    if (crc != CRC_DEFAULT)
    {
        REFLECT_SCOPE_TIMER( ("CRC Check") );

        PROFILE_SCOPE_ACCUM(g_ChecksumAccum);

        uint32_t count = 0;
        uint8_t block[CRC_BLOCK_SIZE];
        memset(block, 0, CRC_BLOCK_SIZE);

        // roll through file
        while (!m_Stream->Done())
        {
            // read block
            m_Stream->ReadBuffer(block, CRC_BLOCK_SIZE);

            // how much we got
            uint32_t got = (uint32_t) m_Stream->ElementsRead();

            // crc block
            current_crc = Helium::UpdateCrc32(current_crc, block, got);

#ifdef REFLECT_DEBUG_BINARY_CRC
            Log::Print("CRC %d (length %d) for datum 0x%08x is 0x%08x\n", count++, got, *(uint32_t*)block, current_crc);
#endif
        }

        // check result
        if (crc != current_crc && !g_OverrideCRC)
        {
            if (crc == CRC_INVALID)
            {
                throw Reflect::ChecksumException( TXT( "Corruption detected, file was not successfully written (incomplete CRC)" ), current_crc, crc );
            }
            else
            {
                throw Reflect::ChecksumException( TXT( "Corruption detected, crc is 0x%08x, should be 0x%08x" ), current_crc, crc);
            }
        }

        // clear error bits
        m_Stream->Clear();

        // seek back to past our crc data to start reading our valid file
        m_Stream->SeekRead(start, std::ios_base::beg);
    }

    // set m_Size to be the size of just the object block
    m_Size = (long) (m_Size - start); 

    // deserialize main file elements
    {
        REFLECT_SCOPE_TIMER( ("Main Spool Read") );

        Deserialize(m_Spool, ArchiveFlags::Status);
    }

    // invalidate the search type and abort flags so we process the append block
    const Class* searchClass = m_SearchClass;
    if ( m_SearchClass != NULL )
    {
        m_SearchClass = NULL;
        m_Skip = false;
    }

    // restore state, just in case someone wants to consume this after the fact
    m_SearchClass = searchClass;

    info.m_ArchiveState = ArchiveStates::Complete;
    e_Status.Raise( info );
}

void ArchiveBinary::Write()
{
    REFLECT_SCOPE_TIMER( ("Reflect - Binary Write") );

    StatusInfo info( *this, ArchiveStates::Starting );
    e_Status.Raise( info );

    // setup visitors
    PreSerialize();

    // write BOM
    uint16_t feff = 0xfeff;
    m_Stream->Write( &feff ); // byte order mark

    // save character encoding value
    CharacterEncoding encoding;
#ifdef UNICODE
    encoding = CharacterEncodings::UTF_16;
    HELIUM_COMPILE_ASSERT( sizeof(wchar_t) == 2 );
#else
    encoding = CharacterEncodings::ASCII;
    HELIUM_COMPILE_ASSERT( sizeof(char) == 1 );
#endif
    uint8_t encodingByte = (uint8_t)encoding;
    m_Stream->Write(&encodingByte);

    // write version
    HELIUM_ASSERT( m_Version == CURRENT_VERSION );
    m_Stream->Write(&m_Version); 

    // always start with the invalid crc, incase we don't make it to the end
    uint32_t crc = CRC_INVALID;

    // save the offset and write the invalid crc to the stream
    uint32_t crc_offset = (uint32_t)m_Stream->TellWrite();
    m_Stream->Write(&crc);

    // serialize main file elements
    {
        REFLECT_SCOPE_TIMER( ("Main Spool Write") );

        Serialize(m_Spool, ArchiveFlags::Status);
    }

    // CRC
    {
        REFLECT_SCOPE_TIMER( ("CRC Build") );

        uint32_t count = 0;
        uint8_t block[CRC_BLOCK_SIZE];
        memset(&block, 0, CRC_BLOCK_SIZE);

        // make damn sure this didn't change
        HELIUM_ASSERT(crc == CRC_INVALID);

        // reset this local back to default for computation
        crc = Helium::BeginCrc32();

        // seek to our starting point (after crc location)
        m_Stream->SeekRead(crc_offset + sizeof(crc), std::ios_base::beg);

        // roll through file
        while (!m_Stream->Done())
        {
            // read block
            m_Stream->ReadBuffer(block, CRC_BLOCK_SIZE);

            // how much we got
            uint32_t got = (uint32_t) m_Stream->ElementsRead();

            // crc block
            crc = Helium::UpdateCrc32(crc, block, got);

#ifdef REFLECT_DEBUG_BINARY_CRC
            Log::Print("CRC %d (length %d) for datum 0x%08x is 0x%08x\n", count++, got, *(uint32_t*)block, crc);
#endif
        }

        // clear errors
        m_Stream->Clear();

        // if we just so happened to hit the invalid crc, disable crc checking
        if (crc == CRC_INVALID)
        {
            crc = CRC_DEFAULT;
        }

        // seek back and write our crc data
        m_Stream->SeekWrite(crc_offset, std::ios_base::beg);
        HELIUM_ASSERT(!m_Stream->Fail());
        m_Stream->Write(&crc); 

    }

    // do cleanup
    m_Stream->SeekWrite(0, std::ios_base::end);
    m_Stream->Flush();

#ifdef REFLECT_DEBUG_BINARY_CRC
    Debug("File written with size %d, crc 0x%08x\n", m_Stream->TellWrite(), crc);
#endif

    info.m_ArchiveState = ArchiveStates::Complete;
    e_Status.Raise( info );
}

void ArchiveBinary::Serialize(const ElementPtr& element)
{
    // use the string pool index for this type's name
    uint32_t classCrc = Helium::Crc32( *element->GetClass()->m_Name );
    m_Stream->Write(&classCrc); 

    // get and stub out the start offset where we are now (will become length after writing is done)
    uint32_t start_offset = (uint32_t)m_Stream->TellWrite();
    m_Stream->Write(&start_offset); 

#ifdef REFLECT_ARCHIVE_VERBOSE
    m_Indent.Get(stdout);
    Log::Debug( TXT( "Serializing %s\n" ), *element->GetClass()->m_Name );
    m_Indent.Push();
#endif

    PreSerialize(element);

    element->PreSerialize();

    if (element->HasType(Reflect::GetType<Data>()))
    {
        Data* s = DangerousCast<Data>(element);

        s->Serialize(*this);
    }
    else
    {
        // push a new struct on the stack
        WriteFields data;
        data.m_Count = 0;
        data.m_CountOffset = m_Stream->TellWrite();
        m_FieldStack.push(data);

        // write some placeholder info
        m_Stream->Write(&m_FieldStack.top().m_Count);

        SerializeFields(element);

        // write our terminator
        const static int32_t terminator = -1;
        m_Stream->Write(&terminator); 

        // seek back and write our count
        HELIUM_ASSERT(m_FieldStack.size() > 0);
        m_Stream->SeekWrite(m_FieldStack.top().m_CountOffset, std::ios_base::beg);
        m_Stream->Write(&m_FieldStack.top().m_Count); 
        m_FieldStack.pop();

        // seek back to end
        m_Stream->SeekWrite(0, std::ios_base::end);
    }

    element->PostSerialize();

    // save our end offset to substract the start from
    uint32_t end_offset = (uint32_t)m_Stream->TellWrite();

    // seek back to the start offset
    m_Stream->SeekWrite(start_offset, std::ios_base::beg);

    // compute amound written
    uint32_t length = end_offset - start_offset;

    // write written amount at start offset
    m_Stream->Write(&length); 

    // seek back to the end of the stream
    m_Stream->SeekWrite(0, std::ios_base::end);

#ifdef REFLECT_ARCHIVE_VERBOSE
    m_Indent.Pop();
#endif
}

void ArchiveBinary::Serialize(const std::vector< ElementPtr >& elements, uint32_t flags)
{
    int32_t size = (int32_t)elements.size();
    m_Stream->Write(&size); 

#ifdef REFLECT_ARCHIVE_VERBOSE
    m_Indent.Get(stdout);
    Log::Debug(TXT("Serializing %d elements\n"), elements.size());
    m_Indent.Push();
#endif

    std::vector< ElementPtr >::const_iterator itr = elements.begin();
    std::vector< ElementPtr >::const_iterator end = elements.end();
    for (int index = 0; itr != end; ++itr, ++index )
    {
        Serialize(*itr);

        if ( flags & ArchiveFlags::Status )
        {
            StatusInfo info( *this, ArchiveStates::ElementProcessed );
            info.m_Progress = (int)(((float)(index) / (float)elements.size()) * 100.0f);
            e_Status.Raise( info );
        }
    }

    if ( flags & ArchiveFlags::Status )
    {
        StatusInfo info( *this, ArchiveStates::ElementProcessed );
        info.m_Progress = 100;
        e_Status.Raise( info );
    }

#ifdef REFLECT_ARCHIVE_VERBOSE
    m_Indent.Pop();
#endif

    const static int32_t terminator = -1;
    m_Stream->Write(&terminator); 
}

void ArchiveBinary::SerializeFields( const ElementPtr& element )
{
    const Composite* composite = element->GetClass();

    std::stack< const Composite* > bases;
    for ( const Composite* current = composite; current != NULL; current = current->m_Base )
    {
        bases.push( current );
    }

    while ( !bases.empty() )
    {
        const Composite* current = bases.top();
        bases.pop();

        std::vector< ConstFieldPtr >::const_iterator itr = current->m_Fields.begin();
        std::vector< ConstFieldPtr >::const_iterator end = current->m_Fields.end();
        for ( ; itr != end; ++itr )
        {
            const Field* field = *itr;

            // don't write no write fields
            if ( field->m_Flags & FieldFlags::Discard )
            {
                return;
            }

            // construct serialization object
            ElementPtr e;
            m_Cache.Create( field->m_DataClass, e );

            HELIUM_ASSERT( e.ReferencesObject() );

            // downcast data
            DataPtr data = ObjectCast<Data>(e);

            if (!data.ReferencesObject())
            {
                // this should never happen, the type id in the rtti data is bogus
                throw Reflect::TypeInformationException( TXT( "Invalid type id for field '%s'" ), field->m_Name.c_str() );
            }

            // set data pointer
            data->ConnectField(element.Ptr(), field);

            // bool for test results
            bool serialize = true;

            // check for equality
            if ( serialize && field->m_Default.ReferencesObject() )
            {
                bool force = (field->m_Flags & FieldFlags::Force) != 0;
                if (!force && field->m_Default->Equals(data))
                {
                    serialize = false;
                }
            }

            // don't write empty containers
            if ( serialize && e->HasType( Reflect::GetType<ContainerData>() ) )
            {
                ContainerDataPtr container = DangerousCast<ContainerData>(e);

                if ( container->GetSize() == 0 )
                {
                    serialize = false;
                }
            }

            // last chance to not write, call through virtual API
            if (serialize)
            {
                PreSerialize(element, field);

                uint32_t fieldNameCrc = Crc32( field->m_Name.c_str() );
                m_Stream->Write(&fieldNameCrc); 

#ifdef REFLECT_ARCHIVE_VERBOSE
                m_Indent.Get(stdout);
                Log::Debug(TXT("Serializing field %s (class %s)\n"), field->m_Name.c_str(), field->m_Index);
                m_Indent.Push();
#endif

                // process
                Serialize( data );

#ifdef REFLECT_ARCHIVE_VERBOSE
                m_Indent.Pop();
#endif

                // we wrote a field, so increment our count
                HELIUM_ASSERT(m_FieldStack.size() > 0);
                m_FieldStack.top().m_Count++;
            }

            // disconnect
            data->Disconnect();
        }
    }
}

ElementPtr ArchiveBinary::Allocate()
{
    ElementPtr element;

    // read type string
    uint32_t typeCrc = Helium::BeginCrc32();
    m_Stream->Read(&typeCrc); 
    const Class* type = Reflect::Registry::GetInstance()->GetClass( typeCrc );

    // read length info if we have it
    uint32_t length = 0;
    m_Stream->Read(&length);

    if (m_Skip)
    {
        // skip it, but account for already reading the length from the stream
        m_Stream->SeekRead(length - sizeof(uint32_t), std::ios_base::cur);
    }
    else
    {
        // allocate instance by name
        m_Cache.Create( type, element );

        // if we failed
        if (!element.ReferencesObject())
        {
            // skip it, but account for already reading the length from the stream
            m_Stream->SeekRead(length - sizeof(uint32_t), std::ios_base::cur);

            // if you see this, then data is being lost because:
            //  1 - a type was completely removed from the codebase
            //  2 - a type was not found because its type library is not registered
            Log::Debug( TXT( "Unable to create object of type '%s', size %d, skipping...\n" ), *type->m_Name, length);
#pragma TODO("Support blind data")
        }
    }

    return element;
}

void ArchiveBinary::Deserialize(ElementPtr& element)
{
    //
    // If we don't have an object allocated for deserialization, pull one from the stream
    //

    if (!element.ReferencesObject())
    {
        element = Allocate();
    }

    //
    // We should now have an instance (unless data was skipped)
    //

    if (element.ReferencesObject())
    {
#ifdef REFLECT_ARCHIVE_VERBOSE
        m_Indent.Get(stdout);
        Log::Debug(TXT("Deserializing %s\n"), *element->GetClass()->m_Name, element->GetType());
        m_Indent.Push();
#endif

        element->PreDeserialize();

        if (element->HasType(Reflect::GetType<Data>()))
        {
            Data* s = DangerousCast<Data>(element);

            s->Deserialize(*this);
        }
        else
        {
            DeserializeFields(element);
        }

        if ( !TryElementCallback( element, &Element::PostDeserialize ) )
        {
            element = NULL; // discard the object
        }

        if ( element )
        {
            PostDeserialize(element);
        }

#ifdef REFLECT_ARCHIVE_VERBOSE
        m_Indent.Pop();
#endif
    }
}

void ArchiveBinary::Deserialize(std::vector< ElementPtr >& elements, uint32_t flags)
{
    uint32_t start_offset = (uint32_t)m_Stream->TellRead();

    int32_t element_count = -1;
    m_Stream->Read(&element_count); 

#ifdef REFLECT_ARCHIVE_VERBOSE
    m_Indent.Get(stdout);
    Log::Debug(TXT("Deserializing %d elements\n"), element_count);
    m_Indent.Push();
#endif

    if (element_count > 0)
    {
        for (int i=0; i<element_count && !m_Abort; i++)
        {
            ElementPtr element;
            Deserialize(element);

            if (element.ReferencesObject())
            {
                if ( element->HasType( m_SearchClass ) )
                {
                    m_Skip = true;
                }

                if ( flags & ArchiveFlags::Status )
                {
                    uint32_t current = (uint32_t)m_Stream->TellRead();

                    StatusInfo info( *this, ArchiveStates::ElementProcessed );
                    info.m_Progress = (int)(((float)(current - start_offset) / (float)m_Size) * 100.0f);
                    e_Status.Raise( info );

                    m_Abort |= info.m_Abort;
                }
            }

            if (element.ReferencesObject() || flags & ArchiveFlags::Sparse)
            {
                elements.push_back( element );
            }
        }
    }

#ifdef REFLECT_ARCHIVE_VERBOSE
    m_Indent.Pop();
#endif

    if (!m_Abort)
    {
        int32_t terminator = -1;
        m_Stream->Read(&terminator);
        if (terminator != -1)
        {
            throw Reflect::DataFormatException( TXT( "Unterminated element array block" ) );
        }
    }

    if ( flags & ArchiveFlags::Status )
    {
        StatusInfo info( *this, ArchiveStates::ElementProcessed );
        info.m_Progress = 100;
        e_Status.Raise( info );
    }
}

void ArchiveBinary::DeserializeFields(const ElementPtr& element)
{
    int32_t fieldCount = -1;
    m_Stream->Read(&fieldCount); 

    for (int i=0; i<fieldCount; i++)
    {
        uint32_t fieldNameCrc = BeginCrc32();
        m_Stream->Read( &fieldNameCrc );

        const Class* type = element->GetClass();
        HELIUM_ASSERT( type );
        const Field* field = type->FindFieldByName(fieldNameCrc);
        HELIUM_ASSERT( field );

#ifdef REFLECT_ARCHIVE_VERBOSE
        m_Indent.Get(stdout);
        Log::Debug(TXT("Deserializing field %s\n"), field->m_Name.c_str());
        m_Indent.Push();
#endif

        // our missing component
        ElementPtr component;

        if ( field )
        {
            // pull and element and downcast to data
            DataPtr latent_data = ObjectCast<Data>( Allocate() );
            if (!latent_data.ReferencesObject())
            {
                // this should never happen, the type id read from the file is bogus
                throw Reflect::TypeInformationException( TXT( "Unknown data for field '%s'" ), field->m_Name.c_str() );
#pragma TODO("Support blind data")
            }

            // if the types match we are a natural fit to just deserialize directly into the field data
            if ( field->m_DataClass == field->m_DataClass )
            {
                // set data pointer
                latent_data->ConnectField( element.Ptr(), field );

                // process natively
                Deserialize( (ElementPtr&)latent_data );

                // post process
                PostDeserialize( element, field );

                // disconnect
                latent_data->Disconnect();
            }
            else // else the type does not match, deserialize it into temp data then attempt to cast it into the field data
            {
                REFLECT_SCOPE_TIMER(("Casting"));

                // construct current serialization object
                ElementPtr current_element;
                m_Cache.Create( field->m_DataClass, current_element );

                // downcast to data
                DataPtr current_data = ObjectCast<Data>(current_element);
                if (!current_data.ReferencesObject())
                {
                    // this should never happen, the type id in the rtti data is bogus
                    throw Reflect::TypeInformationException( TXT( "Invalid type id for field '%s'" ), field->m_Name.c_str() );
                }

                // process into temporary memory
                current_data->ConnectField(element.Ptr(), field);

                // process natively
                Deserialize( (ElementPtr&)latent_data );

                // attempt cast data into new definition
                if ( !Data::CastValue( latent_data, current_data, DataFlags::Shallow ) )
                {
                    // to the component block!
                    component = latent_data;
                }
                else
                {
                    // post process
                    PostDeserialize( element, field );
                }

                // disconnect
                current_data->Disconnect();
            }
        }
        else // else the field does not exist in the current class anymore
        {
            try
            {
                Deserialize( component );
            }
            catch (Reflect::LogisticException& ex)
            {
                Log::Debug( TXT( "Unable to deserialize %s::%s into component (%s), discarding\n" ), *type->m_Name, field->m_Name.c_str(), ex.What());
            }
        }

        if ( component.ReferencesObject() )
        {
            // attempt processing
            if (!element->ProcessComponent(component, field->m_Name))
            {
                Log::Debug( TXT( "%s did not process %s, discarding\n" ), *element->GetClass()->m_Name, *component->GetClass()->m_Name );
            }
        }

#ifdef REFLECT_ARCHIVE_VERBOSE
        m_Indent.Pop();
#endif
    }

    int32_t terminator = -1;
    m_Stream->Read(&terminator); 
    if (terminator != -1)
    {
        throw Reflect::DataFormatException( TXT( "Unterminated field array block" ) );
    }
}

void ArchiveBinary::ToStream( const ElementPtr& element, std::iostream& stream )
{
    std::vector< ElementPtr > elements(1);
    elements[0] = element;
    ToStream( elements, stream );
}

ElementPtr ArchiveBinary::FromStream( std::iostream& stream, const Class* searchClass )
{
    if ( searchClass == NULL )
    {
        searchClass = Reflect::GetClass<Element>();
    }

    ArchiveBinary archive;
    archive.m_SearchClass = searchClass;

    Reflect::CharStreamPtr charStream = new Reflect::Stream<char>( &stream ); 
    archive.OpenStream( charStream, false );
    archive.Read();
    archive.Close(); 

    std::vector< ElementPtr >::iterator itr = archive.m_Spool.begin();
    std::vector< ElementPtr >::iterator end = archive.m_Spool.end();
    for ( ; itr != end; ++itr )
    {
        if ((*itr)->HasType(searchClass))
        {
            return *itr;
        }
    }

    return NULL;
}

void ArchiveBinary::ToStream( const std::vector< ElementPtr >& elements, std::iostream& stream )
{
    ArchiveBinary archive;

    // fix the spool
    archive.m_Spool = elements;

    Reflect::CharStreamPtr charStream = new Reflect::Stream<char>(&stream); 
    archive.OpenStream( charStream, true );
    archive.Write();   
    archive.Close(); 
}

void ArchiveBinary::FromStream( std::iostream& stream, std::vector< ElementPtr >& elements )
{
    ArchiveBinary archive;

    Reflect::CharStreamPtr charStream = new Reflect::Stream<char>(&stream); 
    archive.OpenStream( charStream, false );
    archive.Read();
    archive.Close(); 

    elements = archive.m_Spool;
}
