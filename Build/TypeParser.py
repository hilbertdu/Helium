##----------------------------------------------------------------------------------------------------------------------
## TypeParser.py
##
## Copyright (C) 2010 WhiteMoon Dreams, Inc.
## All Rights Reserved
##----------------------------------------------------------------------------------------------------------------------

import os
import sys
import re
import stat
import string
import datetime
import getopt  # TODO: Eventually replace with argparse (requires Python 3.2).

def PrintCommandLineUsage():
    print( 'Usage:', os.path.basename( sys.argv[ 0 ] ), '[OPTIONS] moduleName [moduleName ...]', file = sys.stderr )
    print( '    moduleName  Base name of a module source directory', file = sys.stderr )
    print( '', file = sys.stderr )
    print( 'OPTIONS:', file = sys.stderr )
    print( '    -i PATH, --includes=PATH    Search for module include directories within', file = sys.stderr )
    print( '                                PATH instead of the current directory.', file = sys.stderr )
    print( '    -s PATH, --sources=PATH     Search for module source directories within', file = sys.stderr )
    print( '                                PATH instead of the current directory.', file = sys.stderr )
    print( '    -p PREFIX, --prefix=PREFIX  Use PREFIX for module API preprocessor tokens', file = sys.stderr )
    print( '                                (default: "HELIUM_"', file = sys.stderr )

def ModuleToTokenName( moduleName ):
    lastCharacter = None
    wasLastCharacterUpper = False
    wasLastCharacterDot = False
    tokenName = ''
    for rawCharacter in moduleName:
        character = rawCharacter.upper()
        isCharacterUpper = ( character == rawCharacter )
        if wasLastCharacterDot or (not isCharacterUpper and wasLastCharacterUpper and tokenName != ''):
            tokenName += '_'

        if lastCharacter is not None:
            tokenName += lastCharacter

        lastCharacter = character
        wasLastCharacterUpper = isCharacterUpper
        wasLastCharacterDot = (lastCharacter == '.')
        if wasLastCharacterDot:
            lastCharacter = None

    if lastCharacter is not None:
        tokenName += lastCharacter

    return tokenName

# Parse the command line options.
try:
    optionList, moduleList = getopt.getopt( sys.argv[ 1 : ], 'i:s:p:', [ 'includes=', 'sources=', 'prefix=' ] )
except getopt.GetoptError as error:
    print( str( error ), file = sys.stderr )
    print( '', file = sys.stderr )
    PrintCommandLineUsage()
    sys.exit( 2 )

if len( moduleList ) == 0:
    print( 'Missing module listing.', file = sys.stderr )
    print( '', file = sys.stderr )
    PrintCommandLineUsage()
    sys.exit( 2 )

includePath = ''
sourcePath = ''
apiTokenPrefix = 'HELIUM_'

for option, value in optionList:
    if option == '-i' or option == '--includes':
        includePath = value
    elif option == '-s' or option == '--sources':
        sourcePath = value
    elif option == '-p' or option == '--prefix':
        apiTokenPrefix = value

if includePath == '':
    includePath = '.'

if sourcePath == '':
    sourcePath = '.'

try:
    pathStat = os.stat( includePath )
except OSError as error:
    print( 'Error validating include file path "' + includePath + '":', file = sys.stderr )
    print( str( error ), file = sys.stderr )
    sys.exit( 1 )

if not stat.S_ISDIR( pathStat.st_mode ):
    print( 'Include file path "' + includePath + '" is not a directory.', file = sys.stderr )
    sys.exit( 1 )

try:
    pathStat = os.stat( includePath )
except OSError as error:
    print( 'Error validating source file path "' + sourcePath + '":', file = sys.stderr )
    print( str( error ), file = sys.stderr )
    sys.exit( 1 )

if not stat.S_ISDIR( pathStat.st_mode ):
    print( 'Source file path "' + sourcePath + '" is not a directory.', file = sys.stderr )
    sys.exit( 1 )

# Parse each module specified.
typeScopeRegExp = re.compile( r'\b(namespace|class\s+\w+_API|class)\s+(\w+)\b' )
objectDeclRegExp = re.compile( r'\b(?<!#define )L_DECLARE_OBJECT\(\s*\w+\s*,\s*[\w:]+\s*\)' )

sourceFormatString0 = \
'''//----------------------------------------------------------------------------------------------------------------------
// {FILE}
//
// Copyright (C) {YEAR} WhiteMoon Dreams, Inc.
// All Rights Reserved
//----------------------------------------------------------------------------------------------------------------------

// !!! AUTOGENERATED FILE - DO NOT EDIT !!!

#include "{MODULE}Pch.h"
#include "Platform/Assert.h"
#include "Engine/Package.h"

'''

includeListingFormatString = \
'''#include "{INCLUDE}"
'''

sourceFormatString1 = \
'''
static Helium::StrongPtr< Helium::Package > sp{MODULE}TypePackage;

{API_TOKEN_PREFIX}{MODULE_TOKEN}_API Helium::Package* Get{MODULE}TypePackage()
{{
    Helium::Package* pPackage = sp{MODULE}TypePackage;
    if( !pPackage )
    {{
'''

sourceFormatString2Engine = \
'''        HELIUM_VERIFY( Helium::GameObject::InitStaticType() );

        Helium::GameObject* pTypesPackageObject = Helium::GameObject::FindChildOf( NULL, Helium::Name( TXT( "Types" ) ) );
        HELIUM_ASSERT( pTypesPackageObject );
        HELIUM_ASSERT( pTypesPackageObject->IsPackage() );

        Helium::GameObject* pPackageObject = Helium::GameObject::FindChildOf(
            pTypesPackageObject,
            Helium::Name( TXT( "{MODULE}" ) ) );
        HELIUM_ASSERT( pPackageObject );
        HELIUM_ASSERT( pPackageObject->IsPackage() );

        pPackage = Helium::Reflect::AssertCast< Helium::Package >( pPackageObject );
        sp{MODULE}TypePackage = pPackage;
'''

sourceFormatString2Default = \
'''        Helium::GameObject* pTypesPackageObject = Helium::GameObject::FindChildOf( NULL, Helium::Name( TXT( "Types" ) ) );
        HELIUM_ASSERT( pTypesPackageObject );
        HELIUM_ASSERT( pTypesPackageObject->IsPackage() );

        HELIUM_VERIFY( Helium::GameObject::Create< Helium::Package >(
            sp{MODULE}TypePackage,
            Helium::Name( TXT( "{MODULE}" ) ),
            pTypesPackageObject ) );
        pPackage = sp{MODULE}TypePackage;
        HELIUM_ASSERT( pPackage );
'''

sourceFormatString3 = \
'''    }}

    return pPackage;
}}

{API_TOKEN_PREFIX}{MODULE_TOKEN}_API void Release{MODULE}TypePackage()
{{
    sp{MODULE}TypePackage = NULL;
}}

{API_TOKEN_PREFIX}{MODULE_TOKEN}_API void Register{MODULE}Types()
{{
    HELIUM_VERIFY( Get{MODULE}TypePackage() );

'''

typeRegLineFormatString = \
'''    HELIUM_VERIFY( {CLASS_PATH}::InitStaticType() );
'''

sourceFormatString4 = \
'''}}

{API_TOKEN_PREFIX}{MODULE_TOKEN}_API void Unregister{MODULE}Types()
{{
'''

typeUnregLineFormatString = \
'''    {CLASS_PATH}::ReleaseStaticType();
'''

sourceFormatString5 = \
'''
    Release{MODULE}TypePackage();
}}
'''

currentYear = datetime.datetime.now().year

for module in moduleList:
    # Skip the "Core" module.
    if module == 'Core':
        print( '[I] Skipping module "Core"...' )
        print()
        continue

    # Make sure the directories for the specified module includes and sources
    # exist and are valid directories.
    moduleIncludePath = os.path.join( includePath, module )
    try:
        pathStat = os.stat( moduleIncludePath )
    except OSError as error:
        print( '[E] Error validating module include path "' + moduleIncludePath + '":', file = sys.stderr )
        print( '[E]', str( error ), file = sys.stderr )
        continue

    if not stat.S_ISDIR( pathStat.st_mode ):
        print( '[E] Module include path "' + moduleIncludePath + '" is not a directory.', file = sys.stderr )
        continue

    moduleSourcePath = os.path.join( sourcePath, module )
    try:
        pathStat = os.stat( moduleSourcePath )
    except OSError as error:
        print( '[E] Error validating module source path "' + moduleSourcePath + '":', file = sys.stderr )
        print( '[E]', str( error ), file = sys.stderr )
        continue

    if not stat.S_ISDIR( pathStat.st_mode ):
        print( '[E] Module source path "' + moduleSourcePath + '" is not a directory.', file = sys.stderr )
        continue

    # Parse each include file in the current module for GameObject-based class declarations.
    print( '[I] Processing module "', module, '"', sep = '' )
    try:
        moduleDirListing = os.listdir( moduleIncludePath )
    except:
        print(
            '[E] Error reading the contents of "', moduleIncludePath, '": ', sys.exc_info()[ 1 ],
            sep = '',
            file = sys.stderr )
        print()
        continue

    includeFiles = set()
    classPathNames = []

    for entry in moduleDirListing:
        # Only parse ".h" files.
        if os.path.splitext( entry )[ 1 ] != '.h':
            continue

        entryPath = os.path.join( moduleIncludePath, entry )

        # Skip directories (as if there should be a directory with a ".h" extension, but just in case...).
        pathStat = os.stat( entryPath )
        if stat.S_ISDIR( pathStat.st_mode ):
            continue

        # Parse the current file line-by-line.
        print( '[I] Parsing "', entryPath, '"', sep = '' )

        try:
            entryFile = open( entryPath, 'rt' )
        except:
            print(
                '[E] Error opening "', entryPath, '" for reading: ', sys.exc_info()[ 1 ],
                sep = '',
                file = sys.stderr )
            continue

        entryLines = entryFile.readlines()
        entryFile.close()

        scopeNames = []
        braceLevels = []
        currentBraceLevel = 0
        bInComment = False

        bInEditorBlock = False
        editorBlockIfLevel = 0

        for entryLine in entryLines:
            # Check for preprocessor #if/#ifdef/#ifndef statements.
            if not bInComment:
                if bInEditorBlock:
                    if entryLine.startswith( '#if' ):
                        editorBlockIfLevel += 1
                    elif entryLine.startswith( '#endif' ):
                        if editorBlockIfLevel == 0:
                            bInEditorBlock = False
                        else:
                            editorBlockIfLevel -= 1
                elif entryLine.startswith( '#if L_EDITOR' ):
                    bInEditorBlock = True

            # Strip out comment blocks.
            stripStartIndex = 0
            while stripStartIndex < len( entryLine ):
                if bInComment:
                    commentEndIndex = entryLine.find( '*/', stripStartIndex )
                    if commentEndIndex == -1:
                        entryLine = entryLine[ : stripStartIndex ]
                        break

                    bInComment = False
                    entryLine = entryLine[ : stripStartIndex ] + entryLine[ commentEndIndex + 2 : ]
                    continue

                searchEndIndex = len( entryLine )
                commentStartIndex = entryLine.find( '//', stripStartIndex, searchEndIndex )
                if commentStartIndex != -1:
                    searchEndIndex = commentStartIndex

                blockCommentStartIndex = entryLine.find( '/*', stripStartIndex, searchEndIndex )
                if blockCommentStartIndex != -1:
                    bInComment = True
                    stripStartIndex = blockCommentStartIndex
                    entryLine = entryLine[ : stripStartIndex ] + entryLine[ stripStartIndex + 2 : ]
                    continue

                if commentStartIndex != -1:
                    entryLine = entryLine[ : commentStartIndex ]

                break

            # Parse the current line for namespace declarations, class declarations, braces, and L_DECLARE_OBJECT()
            # macro calls.
            while entryLine != '':
                searchEndIndex = len( entryLine )
                typeScopeResult = typeScopeRegExp.search( entryLine )
                if typeScopeResult != None:
                    searchEndIndex = typeScopeResult.start()

                objectDeclResult = objectDeclRegExp.search( entryLine )
                if objectDeclResult != None:
                    objectDeclResultStart = objectDeclResult.start()
                    if objectDeclResultStart < searchEndIndex:
                        searchEndIndex = objectDeclResultStart
                    else:
                        objectDeclResult = None

                startBraceIndex = entryLine.find( '{', 0, searchEndIndex )
                if startBraceIndex != -1:
                    searchEndIndex = startBraceIndex

                endBraceIndex = entryLine.find( '}', 0, searchEndIndex )

                stripEndIndex = 0
                if endBraceIndex != -1:
                    if currentBraceLevel != 0:
                        currentBraceLevel -= 1
                        if len( braceLevels ) != 0 and braceLevels[ -1 ] >= currentBraceLevel:
                            del scopeNames[ -1 ]
                            del braceLevels[ -1 ]

                    stripEndIndex = endBraceIndex + 1
                elif startBraceIndex != -1:
                    currentBraceLevel += 1
                    stripEndIndex = startBraceIndex + 1
                elif objectDeclResult != None:
                    classPath = '::'.join( scopeNames )
                    if classPath != None and classPath != '':
                        classPathNames += [ ( classPath, bInEditorBlock ) ]
                        includeFiles.add( module + '/' + entry )

                    stripEndIndex = objectDeclResult.end()
                elif typeScopeResult != None:
                    typeScope = typeScopeResult.group( 2 )
                    if len( braceLevels ) != 0 and braceLevels[ -1 ] == currentBraceLevel:
                        scopeNames[ -1 ] = typeScope
                    else:
                        scopeNames += [ typeScope ]
                        braceLevels += [ currentBraceLevel ]

                    stripEndIndex = typeScopeResult.end()
                else:
                    break

                entryLine = entryLine[ stripEndIndex : ]

    classPathCount = len( classPathNames )
    if classPathCount == 0:
        print( '[I] No GameObject-based classes found in ', module, '.', sep = '' )
        print()
        continue

    print( '[I] Found ', classPathCount, ' GameObject-based class(es) in ', module, '.', sep = '' )
    for ( classPath, bInEditorBlock ) in classPathNames:
        if bInEditorBlock:
            classPath = classPath + ' (editor-only)'

        print( '[I] -', classPath )

    print( '[I] Generating type registration.' )
    typeRegSourceFile = module + 'TypeRegistration.cpp'
    typeRegFileContents = sourceFormatString0.format( FILE = typeRegSourceFile, YEAR = currentYear, MODULE = module )

    for includeEntry in includeFiles:
        typeRegFileContents += includeListingFormatString.format( INCLUDE = includeEntry )

    moduleToken = ModuleToTokenName( module )

    typeRegFileContents += sourceFormatString1.format(
        MODULE = module,
        MODULE_TOKEN = moduleToken,
        API_TOKEN_PREFIX = apiTokenPrefix )

    # Special-case handling for Engine module: package creation is performed by the GameObject type registration due to
    # dependencies, so fetch and use its result.
    if module == 'Engine':
        typeRegFileContents += sourceFormatString2Engine.format( MODULE = module )
    else:
        typeRegFileContents += sourceFormatString2Default.format( MODULE = module )

    typeRegFileContents += sourceFormatString3.format(
        MODULE = module,
        MODULE_TOKEN = moduleToken,
        API_TOKEN_PREFIX = apiTokenPrefix )

    bWasInEditorBlock = False
    for ( classPath, bInEditorBlock ) in classPathNames:
        if bInEditorBlock:
            if not bWasInEditorBlock:
                typeRegFileContents += '#if L_EDITOR\n'
        elif bWasInEditorBlock:
            typeRegFileContents += '#endif\n'

        bWasInEditorBlock = bInEditorBlock

        typeRegFileContents += typeRegLineFormatString.format( CLASS_PATH = classPath )

    if bWasInEditorBlock:
        typeRegFileContents += '#endif\n'

    typeRegFileContents += sourceFormatString4.format(
        MODULE = module,
        MODULE_TOKEN = moduleToken,
        API_TOKEN_PREFIX = apiTokenPrefix )

    bWasInEditorBlock = False
    for ( classPath, bInEditorBlock ) in classPathNames:
        if bInEditorBlock:
            if not bWasInEditorBlock:
                typeRegFileContents += '#if L_EDITOR\n'
        elif bWasInEditorBlock:
            typeRegFileContents += '#endif\n'

        bWasInEditorBlock = bInEditorBlock

        typeRegFileContents += typeUnregLineFormatString.format( CLASS_PATH = classPath )

    if bWasInEditorBlock:
        typeRegFileContents += '#endif\n'

    typeRegFileContents += sourceFormatString5.format( MODULE = module )

    typeRegSourcePath = os.path.join( moduleSourcePath, typeRegSourceFile )
    bWriteFile = True
    try:
        typeRegFile = open( typeRegSourcePath, 'rt' )
    except:
        pass
    else:
        print(
            '[I] Existing source file "', typeRegSourcePath, '" found, comparing with new file contents.', sep = '' )
        existingFileContents = typeRegFile.read()
        typeRegFile.close()

        if typeRegFileContents == existingFileContents:
            print( '[I] Existing source file contents match, file will not be updated.' )
            bWriteFile = False
        else:
            print( '[I] Existing source file differs, file will be updated.' )

    if bWriteFile:
        print( '[I] Writing type registration for module "',  module, '"...', sep = '' )
        try:
            typeRegFile = open( typeRegSourcePath, 'wt' )
        except:
            print(
                '[E] Error opening "', typeRegSource, '" for writing: ', sys.exc_info()[ 1 ],
                sep = '',
                file = sys.stderr )
        else:
            typeRegFile.write( typeRegFileContents )
            typeRegFile.close()
            print( '[I] Finished writing "', typeRegSourcePath, '".', sep = '' )

    print()

print( '[I] Parsing complete.' )
print( '' )

sys.exit( 0 )
