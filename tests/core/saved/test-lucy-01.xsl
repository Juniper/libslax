<?xml version="1.0" encoding="UTF-8" standalone="yes"?>
<xsl:stylesheet xmlns:xsl="http://www.w3.org/1999/XSL/Transform" xmlns:slax-ext="http://xmlsoft.org/XSLT/namespace" xmlns:slax="http://code.google.com/p/libslax/slax" version="1.0" extension-element-prefixes="slax-ext slax">
  <!-- 

  miss lucy had a baby
  she named him tiny tim
  she put him in the bathtub
  to see if he could swim

  he drank up all the water
  he ate up all the soap
  he tried to eat the bathtub
  but is wouldn't go down his throat

  miss lucy called the doctor
  miss lucy called the nurse
  miss lucy called the lady
  with the alligator purse

  mumps said the doctor
  measles said the nurse
  hiccups said the lady
  with the alligator purse

  out went the doctor
  out went the nurse
  out went the lady
  with the alligator purse

 -->
  <xsl:output indent="yes"/>
  <xsl:variable name="verses-temp-1">
    <verse action="baby" where="bathtub" test="swim">
      <xsl:value-of select="1"/>
    </verse>
    <verse action="tub">
      <xsl:value-of select="2"/>
    </verse>
    <verse action="call">
      <xsl:value-of select="3"/>
    </verse>
    <verse action="diagnosis">
      <xsl:value-of select="4"/>
    </verse>
    <verse action="out">
      <xsl:value-of select="5"/>
    </verse>
  </xsl:variable>
  <xsl:variable xmlns:slax-ext="http://xmlsoft.org/XSLT/namespace" name="verses" select="slax-ext:node-set($verses-temp-1)"/>
  <xsl:template match="/">
    <top>
      <xsl:variable name="miss-lucy-temp-2">
        <name>Miss Lucy</name>
        <xsl:call-template name="baby">
          <xsl:with-param name="name" select="&quot;Tiny Tim&quot;"/>
        </xsl:call-template>
      </xsl:variable>
      <xsl:variable xmlns:slax-ext="http://xmlsoft.org/XSLT/namespace" name="miss-lucy" select="slax-ext:node-set($miss-lucy-temp-2)"/>
      <xsl:variable name="bathtub-temp-3">
        <xsl:call-template name="swim">
          <xsl:with-param name="miss-lucy" select="$miss-lucy"/>
        </xsl:call-template>
      </xsl:variable>
      <xsl:variable xmlns:slax-ext="http://xmlsoft.org/XSLT/namespace" name="bathtub" select="slax-ext:node-set($bathtub-temp-3)"/>
      <xsl:variable name="baby-temp-4">
        <water>drank</water>
        <soap>
          <xsl:text>ate </xsl:text>
        </soap>
        <bathtub>
          <epic-fail>throat</epic-fail>
        </bathtub>
      </xsl:variable>
      <xsl:variable xmlns:slax-ext="http://xmlsoft.org/XSLT/namespace" name="baby" select="slax-ext:node-set($baby-temp-4)"/>
      <xsl:variable name="who" select="top/resources"/>
      <xsl:for-each select="$verses">
        <xsl:apply-templates>
          <xsl:with-param name="who" select="$who"/>
          <xsl:with-param name="miss-lucy" select="$miss-lucy"/>
          <xsl:with-param name="bathtub" select="$bathtub"/>
          <xsl:with-param name="baby" select="$baby"/>
        </xsl:apply-templates>
      </xsl:for-each>
    </top>
  </xsl:template>
  <xsl:template name="baby">
    <xsl:param name="name"/>
    <baby>
      <xsl:value-of select="$name"/>
    </baby>
  </xsl:template>
  <xsl:template name="swim">
    <xsl:param name="who"/>
    <swim>
      <xsl:copy-of select="$who"/>
    </swim>
  </xsl:template>
  <xsl:template match="verse[@action = &quot;baby&quot;]">
    <xsl:param name="miss-lucy"/>
    <xsl:param name="baby"/>
    <slax:trace xmlns:slax="http://code.google.com/p/libslax/slax" select="&quot;verse 1&quot;"/>
    <line>
      <xsl:value-of select="concat($miss-lucy/name, &quot; had a &quot;, @action)"/>
    </line>
    <line>
      <xsl:value-of select="concat(&quot;They named it &quot;, $miss-lucy/baby)"/>
    </line>
    <line>
      <xsl:value-of select="concat(&quot;They put it in the &quot;, @where)"/>
    </line>
    <line>
      <xsl:value-of select="concat(&quot;To see if it could &quot;, @test)"/>
    </line>
  </xsl:template>
  <xsl:template match="verse[@action = &quot;tub&quot;]"/>
  <xsl:template match="verse[@action = &quot;call&quot;]"/>
  <xsl:template match="verse[@action = &quot;diagnosis&quot;]"/>
  <xsl:template match="verse[@action = &quot;out&quot;]"/>
</xsl:stylesheet>
