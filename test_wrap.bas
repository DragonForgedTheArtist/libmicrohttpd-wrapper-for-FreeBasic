#Include "windows.bi"
#define PORT 8080

#inclib "mhd_wrap"

Type FB_HANDLER As Function Cdecl( _
    ByVal connection As Any ptr, _
    ByVal url As ZString Ptr, _
    ByVal headers As ZString ptr, _
    ByVal query As ZString ptr, _
    ByVal method As ZString Ptr, _
    ByVal req As UByte Ptr, ByVal req_len As Long, _
    ByVal outp_ As UByte Ptr, ByVal out_cap As Long _
) As Long

declare function mhd_start cdecl alias "mhd_start" (ByVal port_ As UShort) As Long
declare sub mhd_stop cdecl alias "mhd_stop" ()
declare sub mhd_reply_text cdecl alias "mhd_reply_text" (ByVal c As Any Ptr, code as Long, mime_type As ZString Ptr, buff as ZString ptr)
declare sub mhd_reply_bytes cdecl alias "mhd_reply_bytes" (ByVal c As Any Ptr, code as Long, mime_type As ZString Ptr, buff as ZString ptr, length As Long)
declare sub seth cdecl alias "mhd_set_handler" (ByVal fn As Any Ptr)

Function MyHandler Cdecl( _
    ByVal connection As Any ptr, _
    ByVal url As ZString Ptr, _
    ByVal headers As ZString ptr, _
    ByVal query As ZString ptr, _
    ByVal method As ZString ptr, _
    ByVal req As ZString Ptr, ByVal req_len As Long, _
    ByVal outp_ As UByte Ptr, ByVal out_cap As Long _
) As Long
    If *method = "GET" Then
        If *url = "/health" Then
            Dim resp As String = "OK"

            If Len(resp) > out_cap Then
                Return -1
            End If
            mhd_reply_bytes(connection, 200,"text/plain; charset=utf-8", resp, len(resp))
'            CopyMemory(outp_, StrPtr(resp), Len(resp))
            Return Len(resp)

        Else
            ' Copy request into an FB string (UTF-8 bytes)
            Dim s As String = Space(req_len)
            If req_len > 0 Then
                CopyMemory(StrPtr(s), req, req_len)
            End If

            ' TODO: parse s (JSON) and produce response JSON string
            Dim resp As String = "Get Handler " & *url
            mhd_reply_bytes(connection, 200,"text/plain; charset=utf-8", resp, len(resp))

            If Len(resp) > out_cap Then
                Return -1
            End If

'            CopyMemory(outp_, StrPtr(resp), Len(resp))
            Return Len(resp)
        End If
    Elseif *method = "POST" Then
        if *url = "/v1/chat/completions" Then
            ' Copy request into an FB string (UTF-8 bytes)
            Dim s As String = Space(req_len)
            If req_len > 0 Then
                CopyMemory(StrPtr(s), req, req_len)
            End If

            ' TODO: parse s (JSON) and produce response JSON string
            Dim resp As String = "{""id"":""chatcmpl-demo"",""object"":""chat.completion"",""choices"":[{""index"":0,""message"":{""role"":""assistant"",""content"":""hi""}}]}"
            mhd_reply_bytes(connection, 200,"text/plain; charset=utf-8", resp, len(resp))

            If Len(resp) > out_cap Then
                Return -1
            End If

'            CopyMemory(outp_, StrPtr(resp), Len(resp))
            Return Len(resp)
        End If
    End If
End Function

seth( Cast(Any Ptr, @MyHandler) )


If mhd_start(PORT) = 0 Then
    Print "Failed to start server"
    Sleep : End
End If

Print "Server: http://127.0.0.1:" & PORT & "/health"
Print ""

'Dim js As String = "{""kind"":""ingest"",""payload"":{""file"":""rules.pdf""}}"
'mhd_push(StrPtr(js))

Sleep

mhd_stop()
'DyLibFree(h)
