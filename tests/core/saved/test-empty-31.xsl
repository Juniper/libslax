<?xml version="1.0" encoding="UTF-8" standalone="yes"?>
<xsl:stylesheet xmlns:xsl="http://www.w3.org/1999/XSL/Transform" xmlns:slax-ext="http://xmlsoft.org/XSLT/namespace" xmlns:slax="http://xml.libslax.org/slax" version="1.0" extension-element-prefixes="slax-ext slax">
  <xsl:variable name="v1-temp-1">
    <a>a</a>
    <b>b</b>
    <c>c</c>
  </xsl:variable>
  <xsl:variable xmlns:slax-ext="http://xmlsoft.org/XSLT/namespace" name="v1" select="slax-ext:node-set($v1-temp-1)"/>
  <xsl:variable xmlns:xsl="http://www.w3.org/1999/XSL/Transform" name="slax-v2" mvarname="v2">
    <a>a</a>
    <b>b</b>
    <c>c</c>
  </xsl:variable>
  <xsl:variable xmlns:slax="http://xml.libslax.org/slax" name="v2" mutable="yes" select="slax:mvar-init(&quot;v2&quot;, &quot;slax-v2&quot;, $slax-v2)" svarname="slax-v2"/>
  <xsl:variable name="slax-v3" mvarname="v3"/>
  <xsl:variable xmlns:slax="http://xml.libslax.org/slax" name="v3" select="slax:mvar-init(&quot;v3&quot;, &quot;slax-v3&quot;, $slax-v3, $v1)" mutable="yes" svarname="slax-v3"/>
  <xsl:variable name="slax-s1" mvarname="s1"/>
  <xsl:variable xmlns:slax="http://xml.libslax.org/slax" name="s1" select="slax:mvar-init(&quot;s1&quot;, &quot;slax-s1&quot;, $slax-s1, &quot;test&quot;)" mutable="yes" svarname="slax-s1"/>
  <xsl:variable name="slax-s2" mvarname="s2"/>
  <xsl:variable xmlns:slax="http://xml.libslax.org/slax" name="s2" select="slax:mvar-init(&quot;s2&quot;, &quot;slax-s2&quot;, $slax-s2, 45)" mutable="yes" svarname="slax-s2"/>
  <xsl:variable name="slax-s3" mvarname="s3"/>
  <xsl:variable xmlns:slax="http://xml.libslax.org/slax" name="s3" select="slax:mvar-init(&quot;s3&quot;, &quot;slax-s3&quot;, $slax-s3, /asdf)" mutable="yes" svarname="slax-s3"/>
  <xsl:template match="/">
    <top>
      <v1 c="{count($v1)}">
        <xsl:value-of select="$v1/a"/>
      </v1>
      <v2 c="{count($v2)}">
        <xsl:value-of select="$v2/a"/>
      </v2>
      <v3 c="{count($v3)}">
        <xsl:value-of select="$v3/a"/>
      </v3>
      <slax:set-variable xmlns:slax="http://xml.libslax.org/slax" name="s1" svarname="slax-s1" select="$v1"/>
      <s1 c="{count($s1)}">
        <xsl:value-of select="$s1/a"/>
      </s1>
      <slax:set-variable xmlns:slax="http://xml.libslax.org/slax" name="s2" svarname="slax-s2" select="$v2"/>
      <s2 c="{count($s2)}">
        <xsl:value-of select="$s2/a"/>
      </s2>
      <slax:set-variable xmlns:slax="http://xml.libslax.org/slax" name="s3" svarname="slax-s3" select="$v3"/>
      <s3 c="{count($s3)}">
        <xsl:value-of select="$s3/a"/>
      </s3>
      <slax:append-to-variable xmlns:slax="http://xml.libslax.org/slax" name="s1" svarname="slax-s1">
        <d>d</d>
        <e>e</e>
      </slax:append-to-variable>
      <as1>
        <xsl:copy-of select="$s1"/>
      </as1>
      <slax:append-to-variable xmlns:slax="http://xml.libslax.org/slax" name="s1" svarname="slax-s1">
        <f>f</f>
        <g>g</g>
      </slax:append-to-variable>
      <as1>
        <xsl:copy-of select="$s1"/>
      </as1>
    </top>
  </xsl:template>
</xsl:stylesheet>
