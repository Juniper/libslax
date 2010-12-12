<?xml version="1.0" encoding="UTF-8" standalone="yes"?>
<xsl:stylesheet xmlns:xsl="http://www.w3.org/1999/XSL/Transform" version="1.0">
  <xsl:key name="year-born" match="author" use="life-span/born"/>
  <xsl:key name="first-name" match="author" use="name/first"/>
  <xsl:variable name="this-year" select="2010"/>
  <xsl:variable name="upper-case" select="&quot;ABCDEFGHIJKLMNOPQRSTUVWXYZ&quot;"/>
  <xsl:variable name="lower-case" select="&quot;abcdefghijklmnopqrstuvwxyz&quot;"/>
  <xsl:output method="xml" indent="yes"/>
  <xsl:attribute-set name="names">
    <xsl:attribute name="first">
      <xsl:value-of select="name/first"/>
    </xsl:attribute>
    <xsl:attribute name="last">
      <xsl:value-of select="name/last"/>
    </xsl:attribute>
  </xsl:attribute-set>
  <xsl:attribute-set name="age">
    <xsl:attribute name="age-{(life-span/died - life-span/born)}">
      <xsl:value-of select="concat(life-span/born, &quot; - &quot;, life-span/died)"/>
    </xsl:attribute>
  </xsl:attribute-set>
  <xsl:variable name="authors" select="/authors"/>
  <xsl:template match="/">
    <doc>
      <xsl:for-each select="$authors/author">
        <xsl:sort select="name/first"/>
        <xsl:variable name="type">
          <xsl:choose>
            <xsl:when test="life-span/died">
              <xsl:text>dead</xsl:text>
            </xsl:when>
            <xsl:when test="life-span/born">
              <xsl:text>alive</xsl:text>
            </xsl:when>
            <xsl:otherwise>
              <xsl:text>unknown</xsl:text>
            </xsl:otherwise>
          </xsl:choose>
        </xsl:variable>
        <xsl:choose>
          <xsl:when test="life-span/born and life-span/died">
            <xsl:element name="author-{$type}" use-attribute-sets="names age">
              <xsl:attribute name="{translate(name/last, $upper-case, $lower-case)}">
                <xsl:value-of select="name/last"/>
              </xsl:attribute>
              <xsl:call-template name="emit-element"/>
            </xsl:element>
          </xsl:when>
          <xsl:otherwise>
            <xsl:element name="author-{$type}" use-attribute-sets="names">
              <xsl:attribute name="{translate(name/last, $upper-case, $lower-case)}">
                <xsl:value-of select="name/last"/>
              </xsl:attribute>
              <xsl:call-template name="emit-element"/>
            </xsl:element>
          </xsl:otherwise>
        </xsl:choose>
      </xsl:for-each>
      <xsl:for-each select="$authors/author[life-span/born]">
        <xsl:sort select="life-span/born"/>
        <xsl:sort select="life-span/died"/>
        <xsl:choose>
          <xsl:when test="life-span/died">
            <life-span age="{life-span/died - life-span/born}" born="{life-span/born}" died="{life-span/died}">
              <xsl:element name="name" use-attribute-sets="names"/>
            </life-span>
          </xsl:when>
          <xsl:otherwise>
            <life-span age="{$this-year - life-span/born}" born="{life-span/born}">
              <xsl:element name="name" use-attribute-sets="names"/>
            </life-span>
          </xsl:otherwise>
        </xsl:choose>
      </xsl:for-each>
      <!-- Show the oldest five deceased authors -->
      <xsl:for-each select="$authors/author[life-span/born and life-span/died]">
        <xsl:sort select="life-span/died - life-span/born" order="descending"/>
        <xsl:sort select="life-span/born"/>
        <xsl:variable name="gsize" select="1"/>
        <xsl:if test="position() &lt;= 5">
          <xsl:element name="oldest" use-attribute-sets="names">
            <xsl:attribute name="gsize">
              <xsl:value-of select="$gsize"/>
            </xsl:attribute>
            <xsl:attribute name="age">
              <xsl:number value="life-span/died - life-span/born" grouping-size="1" grouping-separator="."/>
            </xsl:attribute>
            <xsl:attribute name="age2">
              <xsl:number value="life-span/died - life-span/born" grouping-size="{$gsize}" grouping-separator="."/>
            </xsl:attribute>
            <xsl:attribute name="born">
              <xsl:number value="life-span/born" format="i"/>
            </xsl:attribute>
            <xsl:attribute name="died">
              <xsl:number value="life-span/died" format="I"/>
            </xsl:attribute>
          </xsl:element>
        </xsl:if>
      </xsl:for-each>
    </doc>
  </xsl:template>
  <xsl:template name="emit-element">
    <xsl:for-each select="name/node()">
      <xsl:copy>
        <xsl:attribute name="source">
          <xsl:text>copy-node</xsl:text>
        </xsl:attribute>
        <xsl:value-of select="."/>
      </xsl:copy>
    </xsl:for-each>
    <xsl:variable name="born" select="life-span/born"/>
    <xsl:variable name="name" select="name"/>
    <xsl:for-each select="$authors">
      <xsl:for-each select="key(&quot;year-born&quot;, $born)">
        <xsl:if test="name/first != $name/first and name/last != $name/last">
          <xsl:element name="born-match" use-attribute-sets="names"/>
        </xsl:if>
      </xsl:for-each>
    </xsl:for-each>
  </xsl:template>
</xsl:stylesheet>
