<?xml version="1.0" encoding="UTF-8" standalone="yes"?>
<xsl:stylesheet xmlns:xsl="http://www.w3.org/1999/XSL/Transform" xmlns:slax="http://xml.libslax.org/slax" version="1.0" extension-element-prefixes="slax">
  <xsl:variable name="errors" mutable="yes"/>
  <xsl:template match="/">
    <top>
      <xsl:apply-templates/>
      <xsl:if test="$errors">
        <errors>
          <xsl:copy-of select="$errors"/>
        </errors>
      </xsl:if>
    </top>
  </xsl:template>
  <xsl:template match="text()"/>
  <xsl:template match="author">
    <xsl:variable name="age" select="life-span/died - life-span/born"/>
    <xsl:variable name="problem" mutable="yes"/>
    <xsl:variable name="tag" select="concat(&quot; (&quot;, $age, &quot; years old)&quot;)" mutable="yes"/>
    <xsl:choose>
      <xsl:when test="$age &lt; 0">
        <slax:set-variable xmlns:slax="http://xml.libslax.org/slax" name="problem" select="&quot;died before born&quot;"/>
      </xsl:when>
      <xsl:when test="$age &lt;= 40">
        <slax:set-variable xmlns:slax="http://xml.libslax.org/slax" name="problem" select="&quot;died too young&quot;"/>
      </xsl:when>
    </xsl:choose>
    <xsl:if test="life-span/died - life-span/born &gt; 65">
      <slax:set-variable xmlns:slax="http://xml.libslax.org/slax" name="problem" select="&quot;lived too long&quot;"/>
    </xsl:if>
    <xsl:if test="not($problem) and life-span/born &lt; 1900">
      <slax:set-variable xmlns:slax="http://xml.libslax.org/slax" name="problem" select="&quot;ancient history&quot;"/>
      <slax:set-variable xmlns:slax="http://xml.libslax.org/slax" name="tag" select="concat(&quot; (c. &quot;, life-span/born, &quot;)&quot;)"/>
    </xsl:if>
    <xsl:if test="$problem">
      <slax:append-to-variable xmlns:slax="http://xml.libslax.org/slax" name="errors">
        <error>
          <name>
            <xsl:value-of select="concat(name/first, &quot; &quot;, name/last)"/>
          </name>
          <message>
            <xsl:value-of select="concat($problem, $tag)"/>
          </message>
        </error>
      </slax:append-to-variable>
    </xsl:if>
  </xsl:template>
</xsl:stylesheet>
