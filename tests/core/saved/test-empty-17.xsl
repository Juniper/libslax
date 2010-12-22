<?xml version="1.0" encoding="UTF-8" standalone="yes"?>
<xsl:stylesheet xmlns:xsl="http://www.w3.org/1999/XSL/Transform" xmlns:slax="http://code.google.com/p/libslax/slax" version="1.0" extension-element-prefixes="slax">
  <xsl:param name="full"/>
  <xsl:template match="/">
    <top>
      <xsl:if test="$full">
        <xsl:variable xmlns:slax="http://code.google.com/p/libslax/slax" name="host" select="slax:get-input(&quot;Please enter hostname: &quot;)"/>
        <xsl:variable xmlns:slax="http://code.google.com/p/libslax/slax" name="pass" select="slax:get-secret(&quot;Please enter password: &quot;)"/>
        <host>
          <xsl:value-of select="$host"/>
        </host>
        <pass>
          <xsl:value-of select="$pass"/>
        </pass>
        <xsl:variable name="list">
          <xsl:for-each xmlns:slax="http://code.google.com/p/libslax/slax" select="slax:build-sequence(1, 10)">
            <cmd>
              <xsl:value-of xmlns:slax="http://code.google.com/p/libslax/slax" select="slax:get-command(&quot;$ &quot;)"/>
            </cmd>
          </xsl:for-each>
        </xsl:variable>
        <list>
          <xsl:copy-of select="$list"/>
        </list>
      </xsl:if>
    </top>
  </xsl:template>
</xsl:stylesheet>
