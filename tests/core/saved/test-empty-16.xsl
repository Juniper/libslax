<?xml version="1.0" encoding="UTF-8" standalone="yes"?>
<xsl:stylesheet xmlns:xsl="http://www.w3.org/1999/XSL/Transform" xmlns:slax="http://xml.libslax.org/slax" xmlns:slax-ext="http://xmlsoft.org/XSLT/namespace" version="1.0" extension-element-prefixes="slax slax-ext">
  <xsl:param name="full"/>
  <xsl:variable name="bad"/>
  <xsl:variable name="good" select="&quot;first&quot;"/>
  <xsl:variable name="data">
    <data>
this is line one
and line two

and line three
and line four
and finally, line five
</data>
  </xsl:variable>
  <!-- 
 * Turns out, back references aren't part of the posix standard, so
 * including them in tests is a Bad Idea (tm).
 -->
  <xsl:param name="force" select="0"/>
  <xsl:template match="/">
    <top>
      <xsl:if test="$force">
        <back-reference>
          <!-- 
	         * The "BUGS" section of the FreeBSD man page for regexec(3) says:
	         *     The back-reference code is subtle and doubts linger
	         *     about its  correctness in complex cases.
                 * Not sure what this means, but this fails under MacOS, while
                 * working correctly under FreeBSD and Linux.  Perl likes it also.
 -->
          <xsl:variable name="pat" select="&quot;([a-z]):\1:\1&quot;"/>
          <xsl:variable xmlns:slax="http://xml.libslax.org/slax" name="re1" select="slax:regex($pat, &quot;a:a:a&quot;)"/>
          <re1>
            <xsl:copy-of select="$re1"/>
          </re1>
          <xsl:variable xmlns:slax="http://xml.libslax.org/slax" name="re2" select="slax:regex($pat, &quot;a:b:c&quot;)"/>
          <re2>
            <xsl:copy-of select="$re2"/>
          </re2>
          <xsl:variable xmlns:slax="http://xml.libslax.org/slax" name="re3" select="slax:regex($pat, &quot;a:b:b:b:a:a&quot;)"/>
          <re3>
            <xsl:copy-of select="$re3"/>
          </re3>
        </back-reference>
      </xsl:if>
      <multiple>
        <xsl:variable name="pat" select="&quot;([a-z]):&quot;"/>
        <xsl:variable xmlns:slax="http://xml.libslax.org/slax" name="re1" select="slax:regex($pat, &quot;a:a:a&quot;)"/>
        <re1>
          <xsl:copy-of select="$re1"/>
        </re1>
        <xsl:variable xmlns:slax="http://xml.libslax.org/slax" name="re2" select="slax:regex($pat, &quot;a:b:c&quot;)"/>
        <re2>
          <xsl:copy-of select="$re2"/>
        </re2>
        <xsl:variable xmlns:slax="http://xml.libslax.org/slax" name="re3" select="slax:regex($pat, &quot;a:b:b:b:a:a&quot;)"/>
        <re3>
          <xsl:copy-of select="$re3"/>
        </re3>
      </multiple>
      <sequence>
        <xsl:for-each xmlns:slax="http://xml.libslax.org/slax" select="slax:build-sequence(1, 3)">
          <xsl:variable name="slax-dot-1" select="."/>
          <xsl:for-each xmlns:slax="http://xml.libslax.org/slax" select="slax:build-sequence(1, 4)">
            <xsl:variable name="i" select="."/>
            <xsl:for-each select="$slax-dot-1">
              <i>
                <xsl:value-of select="concat(., &quot;:&quot;, $i)"/>
              </i>
            </xsl:for-each>
          </xsl:for-each>
        </xsl:for-each>
      </sequence>
      <first>
        <xsl:value-of xmlns:slax="http://xml.libslax.org/slax" select="slax:first-of($bad, worse, $good, &quot;huh&quot;)"/>
      </first>
      <break-lines>
        <xsl:variable xmlns:slax="http://xml.libslax.org/slax" name="bl" select="slax:break-lines($data)"/>
        <xsl:for-each select="$bl">
          <line>
            <xsl:value-of select="."/>
          </line>
        </xsl:for-each>
      </break-lines>
      <printf>
        <xsl:for-each xmlns:slax="http://xml.libslax.org/slax" select="slax:build-sequence(1, 4)">
          <xsl:variable name="boom">
            <xsl:if test="position() = last()">
              <xsl:text> boom!!</xsl:text>
            </xsl:if>
          </xsl:variable>
          <one>
            <xsl:value-of xmlns:slax="http://xml.libslax.org/slax" select="slax:printf(&quot;testing %j1-4d... %4s... %s%s!&quot;, 1, . + 1, &quot;three&quot;, $boom)"/>
          </one>
        </xsl:for-each>
        <two>
          <xsl:value-of xmlns:slax="http://xml.libslax.org/slax" select="slax:printf(&quot;[%jcs] [%jt{test}s] [%jt{test}s]&quot;, &quot;cool&quot;, &quot;ing&quot;, no)"/>
        </two>
      </printf>
      <is-empty>
        <xsl:variable name="e1"/>
        <xsl:variable name="e2-temp-1">
          <test/>
        </xsl:variable>
        <xsl:variable xmlns:slax-ext="http://xmlsoft.org/XSLT/namespace" name="e2" select="slax-ext:node-set($e2-temp-1)"/>
        <xsl:variable name="ne1" select="&quot;test&quot;"/>
        <xsl:variable name="ne2">
          <test/>
        </xsl:variable>
        <xsl:variable name="slax-ne3" mvarname="ne3"/>
        <xsl:variable xmlns:slax="http://xml.libslax.org/slax" name="ne3" mutable="yes" select="slax:mvar-init(&quot;ne3&quot;, &quot;slax-ne3&quot;, $slax-ne3)" svarname="slax-ne3"/>
        <e1>
          <xsl:value-of xmlns:slax="http://xml.libslax.org/slax" select="slax:is-empty($e1)"/>
        </e1>
        <e2>
          <xsl:value-of xmlns:slax="http://xml.libslax.org/slax" select="slax:is-empty($e2/fish)"/>
        </e2>
        <e1>
          <xsl:value-of xmlns:slax="http://xml.libslax.org/slax" select="slax:is-empty($ne3)"/>
        </e1>
        <ne1>
          <xsl:value-of xmlns:slax="http://xml.libslax.org/slax" select="slax:is-empty($ne1)"/>
        </ne1>
        <ne2>
          <xsl:value-of xmlns:slax="http://xml.libslax.org/slax" select="slax:is-empty($ne2)"/>
        </ne2>
        <slax:set-variable xmlns:slax="http://xml.libslax.org/slax" name="ne3" svarname="slax-ne3">
          <test/>
        </slax:set-variable>
        <ne3>
          <xsl:value-of xmlns:slax="http://xml.libslax.org/slax" select="slax:is-empty($ne3)"/>
        </ne3>
      </is-empty>
      <regex>
        <xsl:variable name="data" select="&quot;this is a test of the regex&quot;"/>
        <xsl:variable name="pattern" select="&quot;(.*) test (.*) regex&quot;"/>
        <xsl:variable xmlns:slax="http://xml.libslax.org/slax" name="re1" select="slax:regex($pattern, $data)"/>
        <xsl:for-each select="$re1">
          <re1 position="{position()}">
            <xsl:value-of select="."/>
          </re1>
        </xsl:for-each>
        <xsl:variable xmlns:slax="http://xml.libslax.org/slax" name="re2" select="slax:regex(&quot;([0-9]+)&quot;, $data)"/>
        <xsl:for-each select="$re2">
          <re2 position="{position()}">
            <xsl:value-of select="."/>
          </re2>
        </xsl:for-each>
        <xsl:variable xmlns:slax="http://xml.libslax.org/slax" name="re3" select="slax:regex(&quot;([0-9]+)&quot;, &quot;So..... 99 bottles of ... &quot;)"/>
        <xsl:for-each select="$re3">
          <re3 position="{position()}">
            <xsl:value-of select="."/>
          </re3>
        </xsl:for-each>
      </regex>
      <split>
        <xsl:variable name="in" select="&quot;instructions 1 wash 2 rinse 3 repeat 4 not-reached&quot;"/>
        <xsl:variable xmlns:slax="http://xml.libslax.org/slax" name="sp1" select="slax:split(&quot;[0-9]&quot;, $in)"/>
        <xsl:for-each select="$sp1">
          <sp1 position="{position()}">
            <xsl:value-of select="."/>
          </sp1>
        </xsl:for-each>
        <xsl:variable xmlns:slax="http://xml.libslax.org/slax" name="sp2" select="slax:split(&quot;[0-9]&quot;, $in, 4)"/>
        <xsl:for-each select="$sp2">
          <sp2 position="{position()}">
            <xsl:value-of select="."/>
          </sp2>
        </xsl:for-each>
      </split>
      <xsl:if test="$full">
        <dampen>
          <xsl:for-each xmlns:slax="http://xml.libslax.org/slax" select="slax:build-sequence(1, 100)">
            <xsl:if xmlns:slax="http://xml.libslax.org/slax" test="slax:dampen(&quot;test&quot;, 4, 5)">
              <works>
                <xsl:value-of select="."/>
              </works>
            </xsl:if>
          </xsl:for-each>
        </dampen>
        <sleep>
          <xsl:message>before</xsl:message>
          <xsl:value-of xmlns:slax="http://xml.libslax.org/slax" select="slax:sleep(3)"/>
          <xsl:message>after</xsl:message>
        </sleep>
        <sysctl>
          <xsl:variable name="names-temp-2">
            <v>kern.ostype</v>
            <v>kern.osrelease</v>
            <v>kern.hostname</v>
          </xsl:variable>
          <xsl:variable xmlns:slax-ext="http://xmlsoft.org/XSLT/namespace" name="names" select="slax-ext:node-set($names-temp-2)"/>
          <xsl:for-each select="$names/v">
            <var name="{.}">
              <xsl:value-of xmlns:slax="http://xml.libslax.org/slax" select="slax:sysctl(.)"/>
            </var>
          </xsl:for-each>
          <var name="kern.maxproc">
            <xsl:value-of xmlns:slax="http://xml.libslax.org/slax" select="slax:sysctl(&quot;kern.maxproc&quot;, &quot;i&quot;)"/>
          </var>
        </sysctl>
        <syslog>
          <xsl:value-of xmlns:slax="http://xml.libslax.org/slax" select="slax:syslog(&quot;daemon.error&quot;, &quot;test&quot;, &quot; message&quot;)"/>
        </syslog>
      </xsl:if>
    </top>
  </xsl:template>
</xsl:stylesheet>
