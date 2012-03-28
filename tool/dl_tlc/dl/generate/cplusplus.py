''' copyright (c) 2010 Fredrik Kihlander, see LICENSE for more info '''

DEFAULT_STD_TYPES = '''
#if defined(_MSC_VER)

    #ifndef DL_STD_TYPES_DEFINED
    #define DL_STD_TYPES_DEFINED
        typedef signed   __int8  int8_t;
        typedef signed   __int16 int16_t;
        typedef signed   __int32 int32_t;
        typedef signed   __int64 int64_t;
        typedef unsigned __int8  uint8_t;
        typedef unsigned __int16 uint16_t;
        typedef unsigned __int32 uint32_t;
        typedef unsigned __int64 uint64_t;
    #endif // DL_STD_TYPES_DEFINED

#elif defined(__GNUC__)
    #include <stdint.h>
#endif
'''

ARRAY_TEMPLATE = '''struct
    {
        %(data_type)s* %(data_name)s;
        %(count_type)s %(count_name)s;
    
    #ifdef __cplusplus
              %(data_type)s& operator[](unsigned int index)        { return %(data_name)s[index]; }
        const %(data_type)s& operator[](unsigned int index) const  { return %(data_name)s[index]; }
    #endif // __cplusplus
    }'''

ARRAY_TEMPLATE_STR = '''struct
    {
        const char** %(data_name)s;
        %(count_type)s %(count_name)s;
    
    #ifdef __cplusplus
        const char*& operator[](unsigned int index)        { return %(data_name)s[index]; }
        const char*& operator[](unsigned int index) const  { return %(data_name)s[index]; }
    #endif // __cplusplus
    }'''
    
CLASS_TEMPLATE = '''
#define %(name)s_TYPE_ID (0x%(typeid)08X)

// size32 %(size32)u, size64 %(size64)u, align32 %(align32)u, align64 %(align64)u
struct%(align_str)s%(name)s
{
#ifdef __cplusplus
    const static %(uint32)s TYPE_ID = 0x%(typeid)08X;
#endif // __cplusplus
    
    %(members)s
};
'''

HEADER_TEMPLATE = '''
/* Autogenerated file for DL-type-library! */

#ifndef %(module)s_H_INCLUDED
#define %(module)s_H_INCLUDED

%(define_pods)s

%(user_code)s

%(structs)s

#endif // %(module)s_H_INCLUDED
'''

verbose = True # TODO: not global plox!!!

# TODO: read this config from user!!!
a_config_here = { 'int8'   : 'int8_t',
                  'int16'  : 'int16_t',
                  'int32'  : 'int32_t',
                  'int64'  : 'int64_t',
                  'uint8'  : 'uint8_t',
                  'uint16' : 'uint16_t',
                  'uint32' : 'uint32_t',
                  'uint64' : 'uint64_t',
                  'fp32'   : 'float',
                  'fp64'   : 'double',
                  'string' : 'const char*' }

def to_cpp_name( the_type ):
    import dl.typelibrary as tl
    
    btype = the_type.base_type()
    
    if isinstance( btype, tl.Type ): return 'struct ' + btype.name
    if isinstance( btype, tl.Enum ): return 'enum ' + btype.name
    return a_config_here.get( btype.name, btype.name )

def emit_member( member ):
    lines = []        
    if verbose:
        lines.append( '// 32bit: size %u, align %u, offset %u' % ( member.type.size().ptr32, member.type.align().ptr32, member.offset.ptr32 ) )
        lines.append( '// 64bit: size %u, align %u, offset %u' % ( member.type.size().ptr64, member.type.align().ptr64, member.offset.ptr64 ) )
    
    if member.comment:
        lines.append( '// %s' % member.comment )
        
    import dl.typelibrary as tl
    
    cpp_name = to_cpp_name( member.type )
    
    if   isinstance( member.type, tl.Type ):            lines.append( '%s %s;'               % ( cpp_name, member.name ) )
    elif isinstance( member.type, tl.Enum ):            lines.append( '%s %s;'          % ( cpp_name, member.name ) )
    elif isinstance( member.type, tl.BuiltinType ):     lines.append( '%s %s;'               % ( cpp_name, member.name ) )
    elif isinstance( member.type, tl.InlineArrayType ): lines.append( '%s %s[%u];'           % ( cpp_name, member.name, member.type.count ) ) 
    elif isinstance( member.type, tl.PointerType ):     lines.append( 'const %s* %s;' % ( cpp_name, member.name ) )
    elif isinstance( member.type, tl.BitfieldType ):    lines.append( '%s %s : %u;'          % ( cpp_name, member.name, member.type.bits ) )
    elif isinstance( member.type, tl.ArrayType ):
        if member.type.base_type().name == 'string':
            lines.append( '%s %s;' % ( ARRAY_TEMPLATE_STR % { 'data_name'  : 'data',
                                                              'count_name' : 'count',
                                                              'count_type' : a_config_here['uint32'] },
                                       member.name ) )
        else:
            lines.append( '%s %s;' % ( ARRAY_TEMPLATE % { 'data_type'  : cpp_name, 
                                                          'data_name'  : 'data',
                                                          'count_name' : 'count',
                                                          'count_type' : a_config_here['uint32'] },
                                       member.name ) )
    else:
        assert False, 'missing type (%s)!' % type( member )
        
    return lines

def emit_struct( type, stream ):
    #if verbose:
    if type.comment:
        print >> stream, '//', type.comment
        
    member_lines = []
    for m in type.members:
        member_lines.extend( emit_member( m ) )
        
    stream.write( CLASS_TEMPLATE % { 'size32'    : type.size().ptr32,
                                     'size64'    : type.size().ptr64,
                                     'align32'   : type.align().ptr32,
                                     'align64'   : type.align().ptr64,
                                     'align_str' : ' ' if not type.useralign else ' DL_ALIGN( %u ) ' % type.align().ptr32,
                                     'name'      : type.name,
                                     'uint32'    : a_config_here['uint32'],
                                     'typeid'    : type.typeid,
                                     'members'   : '\n    '.join( member_lines ) } )
    
def emit_enum( enum, stream ):
    stream.write( 'enum %s\n{' % enum.name )
    
    values = []
    
    for e in enum.values:
        values.append( '\n\t%s = %u' % ( e.header_name, e.value ) )

    stream.write( ','.join( values ) )
    stream.write( '\n};\n\n' )

def generate( typelibrary, config, stream ):
    ''' generates a string containing a cpp-header representing the input typelibrary '''
    
    from StringIO import StringIO
    
    structs = StringIO()
    
    for enum in typelibrary.enums.values():
        emit_enum( enum, structs )
    
    #for type_name in typelibrary.types.keys():
    #    print >> structs, 'struct', type_name, ';' 
    
    for type_name in typelibrary.type_order:
        emit_struct( typelibrary.types[type_name], structs )
    
    stream.write( HEADER_TEMPLATE % { 'module'         : typelibrary.name.upper(),
                                      'define_pods'    : DEFAULT_STD_TYPES,
                                      'user_code'      : '// USER CODE',
                                      'structs'        : structs.getvalue() } )
